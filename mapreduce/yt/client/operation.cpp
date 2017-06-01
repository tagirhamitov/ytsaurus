#include "operation.h"

#include <mapreduce/yt/interface/errors.h>

#include <mapreduce/yt/common/log.h>
#include <mapreduce/yt/common/config.h>
#include <mapreduce/yt/common/serialize.h>
#include <mapreduce/yt/common/fluent.h>
#include <mapreduce/yt/common/helpers.h>

#include <library/yson/writer.h>
#include <library/yson/json_writer.h>

#include <mapreduce/yt/http/requests.h>
#include <mapreduce/yt/http/error.h>

#include <mapreduce/yt/io/job_reader.h>
#include <mapreduce/yt/io/job_writer.h>
#include <mapreduce/yt/io/yamr_table_reader.h>
#include <mapreduce/yt/io/yamr_table_writer.h>
#include <mapreduce/yt/io/node_table_reader.h>
#include <mapreduce/yt/io/node_table_writer.h>
#include <mapreduce/yt/io/proto_table_reader.h>
#include <mapreduce/yt/io/proto_table_writer.h>
#include <mapreduce/yt/io/proto_helpers.h>
#include <mapreduce/yt/io/file_reader.h>

#include <util/string/printf.h>
#include <util/string/builder.h>
#include <util/system/execpath.h>
#include <util/system/rwlock.h>
#include <util/system/mutex.h>
#include <util/folder/path.h>
#include <util/stream/file.h>
#include <util/stream/buffer.h>

#include <library/digest/md5/md5.h>

namespace NYT {

////////////////////////////////////////////////////////////////////////////////

namespace {

ui64 RoundUpFileSize(ui64 size)
{
    constexpr ui64 roundUpTo = 4ull << 10;
    return (size + roundUpTo - 1) & ~(roundUpTo - 1);
}

TString ToString(EMergeMode mode)
{
    switch (mode) {
        case MM_UNORDERED: return "unordered";
        case MM_ORDERED: return "ordered";
        case MM_SORTED: return "sorted";
    }
    Y_UNREACHABLE();
}

bool IsLocalMode(const TAuth& auth)
{
    static yhash<TString, bool> localModeMap;
    static TRWMutex mutex;

    {
        TReadGuard guard(mutex);
        auto it = localModeMap.find(auth.ServerName);
        if (it != localModeMap.end()) {
            return it->second;
        }
    }

    bool isLocalMode = false;
    TString localModeAttr("//sys/@local_mode_fqdn");
    if (Exists(auth, TTransactionId(), localModeAttr)) {
        auto fqdn = NodeFromYsonString(Get(auth, TTransactionId(), localModeAttr)).AsString();
        isLocalMode = (fqdn == TProcessState::Get()->HostName);
    }

    {
        TWriteGuard guard(mutex);
        localModeMap[auth.ServerName] = isLocalMode;
    }

    return isLocalMode;
}

class TOperationTracker
{
public:
    void Start(const TOperationId& operationId)
    {
        with_lock(Lock_) {
            StartTimes_[operationId] = TInstant::Now();
        }
    }

    TString Finish(const TOperationId& operationId)
    {
        TDuration duration;
        with_lock(Lock_) {
            auto i = StartTimes_.find(operationId);
            if (i == StartTimes_.end()) {
                ythrow yexception() <<
                    "Operation " << GetGuidAsString(operationId) << " did not start";
            }
            duration = TInstant::Now() - i->second;
            StartTimes_.erase(i);
        }
        return ToString(duration);
    }

    static TOperationTracker* Get()
    {
        return Singleton<TOperationTracker>();
    }

private:
    yhash<TOperationId, TInstant> StartTimes_;
    TMutex Lock_;
};

////////////////////////////////////////////////////////////////////////////////

class TJobPreparer
    : private TNonCopyable
{
public:
    TJobPreparer(
        const TAuth& auth,
        const TTransactionId& transactionId,
        const TString& commandLineName,
        const TUserJobSpec& spec,
        IJob* job,
        size_t outputTableCount,
        const TMultiFormatDesc& inputDesc,
        const TMultiFormatDesc& outputDesc,
        const TOperationOptions& options)
        : Auth_(auth)
        , TransactionId_(transactionId)
        , Spec_(spec)
        , InputDesc_(inputDesc)
        , OutputDesc_(outputDesc)
        , Options_(options)
    {
        CreateStorage();
        UploadFilesFromSpec();
        UploadJobState(job);
        UploadProtoConfig("proto_input", inputDesc);
        UploadProtoConfig("proto_output", outputDesc);

        BinaryPath_ = GetExecPath();
        if (TConfig::Get()->JobBinary) {
            BinaryPath_ = TConfig::Get()->JobBinary;
        }
        if (Spec_.JobBinary_) {
            BinaryPath_ = *Spec_.JobBinary_;
        }

        TString jobBinaryPath;
        if (!IsLocalMode(auth)) {
            UploadBinary();
            jobBinaryPath = "./cppbinary";
        } else {
            jobBinaryPath = BinaryPath_;
        }

        ClassName_ = TJobFactory::Get()->GetJobName(job);
        Command_ = TStringBuilder() <<
            options.JobCommandPrefix_ <<
            (TConfig::Get()->UseClientProtobuf ? "" : "YT_USE_CLIENT_PROTOBUF=0 ") <<
            jobBinaryPath << " " <<
            commandLineName << " " <<
            "\"" << ClassName_ << "\" " <<
            outputTableCount << " " <<
            HasState_ <<
            options.JobCommandSuffix_;
    }

    const yvector<TRichYPath>& GetFiles() const
    {
        return Files_;
    }

    const TString& GetClassName() const
    {
        return ClassName_;
    }

    const TString& GetCommand() const
    {
        return Command_;
    }

    const TUserJobSpec& GetSpec() const
    {
        return Spec_;
    }

    bool ShouldMountSandbox() const
    {
        return TConfig::Get()->MountSandboxInTmpfs || Options_.MountSandboxInTmpfs_;
    }

    ui64 GetTotalFileSize() const
    {
        return TotalFileSize_;
    }

private:
    TAuth Auth_;
    TTransactionId TransactionId_;
    TUserJobSpec Spec_;
    TMultiFormatDesc InputDesc_;
    TMultiFormatDesc OutputDesc_;
    TOperationOptions Options_;

    TString BinaryPath_;
    yvector<TRichYPath> Files_;
    bool HasState_ = false;
    TString ClassName_;
    TString Command_;
    ui64 TotalFileSize_ = 0;

    static void CalculateMD5(const TString& localFileName, char* buf)
    {
        MD5::File(~localFileName, buf);
    }

    static void CalculateMD5(const TBuffer& buffer, char* buf)
    {
        MD5::Data(reinterpret_cast<const unsigned char*>(buffer.Data()), buffer.Size(), buf);
    }

    static THolder<TInputStream> CreateStream(const TString& localPath)
    {
        return new TMappedFileInput(localPath);
    }

    static THolder<TInputStream> CreateStream(const TBuffer& buffer)
    {
        return new TBufferInput(buffer);
    }

    TString GetFileStorage() const
    {
        return Options_.FileStorage_ ?
            *Options_.FileStorage_ :
            TConfig::Get()->RemoteTempFilesDirectory;
    }

    void CreateStorage() const
    {
        TString cypressFolder = TStringBuilder() << GetFileStorage() << "/hash";
        if (!Exists(Auth_, Options_.FileStorageTransactionId_, cypressFolder)) {
            Create(Auth_, Options_.FileStorageTransactionId_, cypressFolder, "map_node", true, true);
        }
    }

    template <class TSource>
    TString UploadToCache(const TSource& source) const
    {
        constexpr size_t md5Size = 32;
        char buf[md5Size + 1];
        CalculateMD5(source, buf);

        TString twoDigits(buf + md5Size - 2, 2);

        TString cypressPath = TStringBuilder() << GetFileStorage() <<
            "/hash/" << twoDigits << "/" << buf;

        int retryCount = 256;
        for (int attempt = 0; attempt < retryCount; ++attempt) {
            TNode linkAttrs;
            if (Exists(Auth_, Options_.FileStorageTransactionId_, cypressPath + "&")) {
                try {
                    linkAttrs = NodeFromYsonString(
                        Get(Auth_, Options_.FileStorageTransactionId_, cypressPath + "&/@"));
                } catch (TErrorResponse& e) {
                    if (!e.IsResolveError()) {
                        throw;
                    }
                }
            }

            try {
                bool linkExists = false;
                if (linkAttrs.GetType() != TNode::UNDEFINED) {
                    if (linkAttrs["type"] == "link" &&
                        (!linkAttrs.HasKey("broken") || !linkAttrs["broken"].AsBool()))
                    {
                        linkExists = true;
                    } else {
                        Remove(Auth_, Options_.FileStorageTransactionId_, cypressPath + "&", true, true);
                    }
                }

                if (linkExists) {
                    Set(Auth_, Options_.FileStorageTransactionId_, cypressPath + "/@touched", "\"true\"");
                    Set(Auth_, Options_.FileStorageTransactionId_, cypressPath + "&/@touched", "\"true\"");
                    return cypressPath;
                }

                TString uniquePath = TStringBuilder() << GetFileStorage() <<
                    "/" << twoDigits << "/cpp_" << CreateGuidAsString();

                Create(Auth_, Options_.FileStorageTransactionId_, uniquePath, "file", true, true,
                    TNode()("hash", buf)("touched", true));

                {
                    THttpHeader header("PUT", GetWriteFileCommand());
                    header.SetToken(Auth_.Token);
                    header.AddPath(uniquePath);
                    auto streamMaker = [&source] () {
                        return CreateStream(source);
                    };
                    RetryHeavyWriteRequest(Auth_, Options_.FileStorageTransactionId_, header, streamMaker);
                }

                Link(Auth_, Options_.FileStorageTransactionId_, uniquePath, cypressPath, true, true,
                    TNode()("touched", true));

            } catch (TErrorResponse& e) {
                if (!e.IsResolveError() || attempt + 1 == retryCount) {
                    throw;
                }
                Sleep(TDuration::Seconds(1));
                continue;
            }
            break;
        }
        return cypressPath;
    }

    void UploadFilesFromSpec()
    {
        for (const auto& file : Spec_.Files_) {
            if (!Exists(Auth_, TransactionId_, file.Path_)) {
                ythrow yexception() << "File " << file.Path_ << " does not exist";
            }

            if (ShouldMountSandbox()) {
                auto size = NodeFromYsonString(
                    Get(Auth_, TransactionId_, file.Path_ + "/@uncompressed_data_size")
                ).AsInt64();

                TotalFileSize_ += RoundUpFileSize(static_cast<ui64>(size));
            }
        }

        Files_ = Spec_.Files_;

        for (const auto& localFile : Spec_.LocalFiles_) {
            TFsPath path(localFile);
            path.CheckExists();

            TFileStat stat;
            path.Stat(stat);
            bool isExecutable = stat.Mode & (S_IXUSR | S_IXGRP | S_IXOTH);

            auto cachePath = UploadToCache(localFile);

            TRichYPath cypressPath(cachePath);
            cypressPath.FileName(path.Basename());
            if (isExecutable) {
                cypressPath.Executable(true);
            }

            if (ShouldMountSandbox()) {
                TotalFileSize_ += RoundUpFileSize(stat.Size);
            }

            Files_.push_back(cypressPath);
        }
    }

    void UploadBinary()
    {
        if (ShouldMountSandbox()) {
            TFsPath path(BinaryPath_);
            TFileStat stat;
            path.Stat(stat);
            TotalFileSize_ += RoundUpFileSize(stat.Size);
        }

        auto cachePath = UploadToCache(BinaryPath_);
        Files_.push_back(TRichYPath(cachePath)
            .FileName("cppbinary")
            .Executable(true));
    }

    void UploadJobState(IJob* job)
    {
        TBufferOutput output(1 << 20);
        job->Save(output);

        if (output.Buffer().Size()) {
            auto cachePath = UploadToCache(output.Buffer());
            Files_.push_back(TRichYPath(cachePath).FileName("jobstate"));
            HasState_ = true;

            if (ShouldMountSandbox()) {
                TotalFileSize_ += RoundUpFileSize(output.Buffer().Size());
            }
        }
    }

    void UploadProtoConfig(const TString& fileName, const TMultiFormatDesc& desc) {
        if (desc.Format != TMultiFormatDesc::F_PROTO) {
            return;
        }

        TBufferOutput messageTypeList;
        for (const auto& descriptor : desc.ProtoDescriptors) {
            messageTypeList << descriptor->full_name() << Endl;
        }

        auto cachePath = UploadToCache(messageTypeList.Buffer());
        Files_.push_back(TRichYPath(cachePath).FileName(fileName));

        if (ShouldMountSandbox()) {
            TotalFileSize_ += RoundUpFileSize(messageTypeList.Buffer().Size());
        }
    }
};

////////////////////////////////////////////////////////////////////////////////

void DumpOperationStderrs(
    TOutputStream& stream,
    const TAuth& auth,
    const TTransactionId& transactionId,
    const TString& operationPath)
{
    const size_t RESULT_LIMIT = 1 << 20;
    const size_t BLOCK_SIZE = 16 << 10;
    const i64 STDERR_LIMIT = 64 << 10;
    const size_t STDERR_COUNT_LIMIT = 20;

    auto jobsPath = operationPath + "/jobs";
    if (!Exists(auth, transactionId, jobsPath)) {
        return;
    }

    THttpHeader header("GET", "list");
    header.AddPath(jobsPath);
    header.SetParameters(AttributeFilterToYsonString(
        TAttributeFilter()
            .AddAttribute("state")
            .AddAttribute("error")
            .AddAttribute("address")));
    auto jobList = NodeFromYsonString(RetryRequest(auth, header)).AsList();

    TBuffer buffer;
    TBufferOutput output(buffer);
    buffer.Reserve(RESULT_LIMIT);

    size_t count = 0;
    for (auto& job : jobList) {
        auto jobPath = jobsPath + "/" + job.AsString();
        auto& attributes = job.Attributes();
        output << Endl;

        if (!attributes.HasKey("state") || attributes["state"].AsString() != "failed") {
            continue;
        }
        if (attributes.HasKey("address")) {
            output << "Host: " << attributes["address"].AsString() << Endl;
        }
        if (attributes.HasKey("error")) {
            output << "Error: " << NodeToYsonString(attributes["error"]) << Endl;
        }

        auto stderrPath = jobPath + "/stderr";
        if (!Exists(auth, transactionId, stderrPath)) {
            continue;
        }

        output << "Stderr: " << Endl;
        if (buffer.Size() >= RESULT_LIMIT) {
            break;
        }

        TRichYPath path(stderrPath);
        i64 stderrSize = NodeFromYsonString(
            Get(auth, transactionId, stderrPath + "/@uncompressed_data_size")).AsInt64();
        if (stderrSize > STDERR_LIMIT) {
            path.AddRange(
                TReadRange().LowerLimit(
                    TReadLimit().Offset(stderrSize - STDERR_LIMIT)));
        }
        IFileReaderPtr reader = new TFileReader(path, auth, transactionId);

        auto pos = buffer.Size();
        auto left = RESULT_LIMIT - pos;
        while (left) {
            auto blockSize = Min(left, BLOCK_SIZE);
            buffer.Resize(pos + blockSize);
            auto bytes = reader->Load(buffer.Data() + pos, blockSize);
            left -= bytes;
            if (bytes != blockSize) {
                buffer.Resize(pos + bytes);
                break;
            }
            pos += bytes;
        }

        if (left == 0 || ++count == STDERR_COUNT_LIMIT) {
            break;
        }
    }

    stream.Write(buffer.Data(), buffer.Size());
    stream << Endl;
}

using TDescriptorList = yvector<const ::google::protobuf::Descriptor*>;

TMultiFormatDesc IdentityDesc(const TMultiFormatDesc& multi)
{
    const std::set<const ::google::protobuf::Descriptor*> uniqueDescrs(multi.ProtoDescriptors.begin(), multi.ProtoDescriptors.end());
    if (uniqueDescrs.size() > 1)
    {
        TApiUsageError err;
        err << __LOCATION__ << ": Different input proto descriptors";
        for (const auto& desc : multi.ProtoDescriptors) {
            err << " " << desc->full_name();
        }
        throw err;
    }
    TMultiFormatDesc result;
    result.Format = multi.Format;
    result.ProtoDescriptors.assign(uniqueDescrs.begin(), uniqueDescrs.end());
    return result;
}

//TODO: simplify to lhs == rhs after YT-6967 resolving
bool IsCompatible(const TDescriptorList& lhs, const TDescriptorList& rhs)
{
    return lhs.empty() || rhs.empty() || lhs == rhs;
}

const TMultiFormatDesc& MergeIntermediateDesc(const TMultiFormatDesc& lh, const TMultiFormatDesc& rh, const char* lhDescr, const char* rhDescr)
{
    if (rh.Format == TMultiFormatDesc::F_NONE) {
        return lh;
    } else if (lh.Format == TMultiFormatDesc::F_NONE) {
        return rh;
    } else if (lh.Format == rh.Format && IsCompatible(lh.ProtoDescriptors, rh.ProtoDescriptors)) {
        const auto& result = rh.ProtoDescriptors.empty() ? lh : rh;
        if (result.ProtoDescriptors.size() > 1) {
            ythrow TApiUsageError() << "too many proto descriptors for intermediate table";
        }
        return result;
    } else {
        ythrow TApiUsageError() << "incompatible format specifications: "
            << lhDescr << " {format=" << ui32(lh.Format) << " descrs=" << lh.ProtoDescriptors.size() << "}"
               " and "
            << rhDescr << " {format=" << ui32(rh.Format) << " descrs=" << rh.ProtoDescriptors.size() << "}"
        ;
    }
}
} // namespace

////////////////////////////////////////////////////////////////////////////////

TOperationId StartOperation(
    const TAuth& auth,
    const TTransactionId& transactionId,
    const TString& operationName,
    const TString& ysonSpec)
{
    THttpHeader header("POST", operationName);
    header.AddTransactionId(transactionId);
    header.AddMutationId();

    TOperationId operationId = ParseGuidFromResponse(
        RetryRequest(auth, header, ysonSpec, false, true));

    LOG_INFO("Operation %s started (%s)",
        ~GetGuidAsString(operationId), ~operationName);

    TOperationTracker::Get()->Start(operationId);

    return operationId;
}

EOperationStatus CheckOperation(
    const TAuth& auth,
    const TTransactionId& transactionId,
    const TOperationId& operationId)
{
    auto opIdStr = GetGuidAsString(operationId);
    auto opPath = Sprintf("//sys/operations/%s", ~opIdStr);
    auto statePath = opPath + "/@state";

    if (!Exists(auth, transactionId, opPath)) {
        ythrow yexception() << "Operation " << opIdStr << " does not exist";
    }

    TString state = NodeFromYsonString(
        Get(auth, transactionId, statePath)).AsString();

    if (state == "completed") {
        return OS_COMPLETED;

    } else if (state == "aborted" || state == "failed") {
        LOG_ERROR("Operation %s %s (%s)",
            ~opIdStr,
            ~state,
            ~TOperationTracker::Get()->Finish(operationId));

        auto errorPath = opPath + "/@result/error";
        TString error;
        if (Exists(auth, transactionId, errorPath)) {
            error = Get(auth, transactionId, errorPath);
        }

        TStringStream jobErrors;
        DumpOperationStderrs(jobErrors, auth, transactionId, opPath);
        error += jobErrors.Str();
        Cerr << error << Endl;

        ythrow TOperationFailedError(
            state == "aborted" ?
                TOperationFailedError::Aborted :
                TOperationFailedError::Failed,
            operationId,
            error);
    }

    return OS_RUNNING;
}

void WaitForOperation(
    const TAuth& auth,
    const TTransactionId& transactionId,
    const TOperationId& operationId)
{
    const TDuration checkOperationStateInterval =
        IsLocalMode(auth) ? TDuration::MilliSeconds(100) : TDuration::Seconds(1);

    while (true) {
        auto status = CheckOperation(auth, transactionId, operationId);
        if (status == OS_COMPLETED) {
            LOG_INFO("Operation %s completed (%s)",
                ~GetGuidAsString(operationId),
                ~TOperationTracker::Get()->Finish(operationId));
            break;
        }
        Sleep(checkOperationStateInterval);
    }
}

void AbortOperation(
    const TAuth& auth,
    const TTransactionId& transactionId,
    const TOperationId& operationId)
{
    THttpHeader header("POST", "abort_op");
    header.AddTransactionId(transactionId);
    header.AddOperationId(operationId);
    header.AddMutationId();
    RetryRequest(auth, header);
}

////////////////////////////////////////////////////////////////////////////////

namespace {

// TODO: we have inputDesc and outputDesc in TJobPreparer
void BuildUserJobFluently(
    const TJobPreparer& preparer,
    TMaybe<TNode> format,
    const TMultiFormatDesc& inputDesc,
    const TMultiFormatDesc& outputDesc,
    TFluentMap fluent)
{
    TMaybe<i64> memoryLimit = preparer.GetSpec().MemoryLimit_;

    auto tmpfsSize = preparer.GetSpec().ExtraTmpfsSize_.GetOrElse(0LL);
    if (preparer.ShouldMountSandbox()) {
        tmpfsSize += preparer.GetTotalFileSize();
        if (tmpfsSize == 0) {
            // This can be a case for example when it is local mode and we don't upload binary.
            // NOTE: YT doesn't like zero tmpfs size.
            tmpfsSize = RoundUpFileSize(1);
        }
        memoryLimit = memoryLimit.GetOrElse(512ll << 20) + tmpfsSize;
    }

    // TODO: tables as files

    fluent
    .Item("file_paths").List(preparer.GetFiles())
    .DoIf(inputDesc.Format == TMultiFormatDesc::F_YSON, [] (TFluentMap fluentMap)
    {
        fluentMap
        .Item("input_format").BeginAttributes()
            .Item("format").Value("binary")
        .EndAttributes()
        .Value("yson");
    })
    .DoIf(inputDesc.Format == TMultiFormatDesc::F_YAMR, [&] (TFluentMap fluentMap) {
        if (!format) {
            fluentMap
            .Item("input_format").BeginAttributes()
                .Item("lenval").Value(true)
                .Item("has_subkey").Value(true)
                .Item("enable_table_index").Value(true)
            .EndAttributes()
            .Value("yamr");
        } else {
            fluentMap.Item("input_format").Value(format.GetRef());
        }
    })
    .DoIf(inputDesc.Format == TMultiFormatDesc::F_PROTO, [&] (TFluentMap fluentMap)
    {
        if (TConfig::Get()->UseClientProtobuf) {
            fluentMap
            .Item("input_format").BeginAttributes()
                .Item("format").Value("binary")
            .EndAttributes()
            .Value("yson");
        } else {
            if (inputDesc.ProtoDescriptors.empty()) {
                ythrow TApiUsageError() << "messages for input_format are unknown (empty ProtoDescriptors)";
            }
            auto config = MakeProtoFormatConfig(inputDesc.ProtoDescriptors);
            fluentMap.Item("input_format").Value(config);
        }
    })
    .DoIf(outputDesc.Format == TMultiFormatDesc::F_YSON, [] (TFluentMap fluentMap)
    {
        fluentMap
        .Item("output_format").BeginAttributes()
            .Item("format").Value("binary")
        .EndAttributes()
        .Value("yson");
    })
    .DoIf(outputDesc.Format == TMultiFormatDesc::F_YAMR, [] (TFluentMap fluentMap) {
        fluentMap
        .Item("output_format").BeginAttributes()
            .Item("lenval").Value(true)
            .Item("has_subkey").Value(true)
        .EndAttributes()
        .Value("yamr");
    })
    .DoIf(outputDesc.Format == TMultiFormatDesc::F_PROTO, [&] (TFluentMap fluentMap)
    {
        if (TConfig::Get()->UseClientProtobuf) {
            fluentMap
            .Item("output_format").BeginAttributes()
                .Item("format").Value("binary")
            .EndAttributes()
            .Value("yson");
        } else {
            if (outputDesc.ProtoDescriptors.empty()) {
                ythrow TApiUsageError() << "messages for output_format are unknown (empty ProtoDescriptors)";
            }
            auto config = MakeProtoFormatConfig(outputDesc.ProtoDescriptors);
            fluentMap.Item("output_format").Value(config);
        }
    })
    .Item("command").Value(preparer.GetCommand())
    .Item("class_name").Value(preparer.GetClassName())
    .DoIf(memoryLimit.Defined(), [&] (TFluentMap fluentMap) {
        fluentMap.Item("memory_limit").Value(*memoryLimit);
    })
    .DoIf(preparer.ShouldMountSandbox(), [&] (TFluentMap fluentMap) {
        fluentMap.Item("tmpfs_path").Value(".");
        fluentMap.Item("tmpfs_size").Value(tmpfsSize);
        fluentMap.Item("copy_files").Value(true);
    });
}

void BuildCommonOperationPart(const TOperationOptions& options, TFluentMap fluent)
{
    const TProcessState* properties = TProcessState::Get();
    const TString& pool = TConfig::Get()->Pool;

    fluent
        .Item("started_by")
        .BeginMap()
            .Item("hostname").Value(properties->HostName)
            .Item("pid").Value(properties->Pid)
            .Item("user").Value(properties->UserName)
            .Item("command").List(properties->CommandLine)
            .Item("wrapper_version").Value(properties->ClientVersion)
        .EndMap()
        .DoIf(!pool.Empty(), [&] (TFluentMap fluentMap) {
            fluentMap.Item("pool").Value(pool);
        })
        .DoIf(options.SecureVault_.Defined(), [&] (TFluentMap fluentMap) {
            if (!options.SecureVault_->IsMap()) {
                ythrow yexception() << "SecureVault must be a map node";
            }
            fluentMap.Item("secure_vault").Value(*options.SecureVault_);
        });
}

template <typename TSpec>
void BuildCommonUserOperationPart(const TSpec& baseSpec, TNode* spec)
{
    if (baseSpec.MaxFailedJobCount_.Defined()) {
        (*spec)["max_failed_job_count"] = *baseSpec.MaxFailedJobCount_;
    }
    if (baseSpec.StderrTablePath_.Defined()) {
        (*spec)["stderr_table_path"] = *baseSpec.StderrTablePath_;
    }
    if (baseSpec.CoreTablePath_.Defined()) {
        (*spec)["core_table_path"] = *baseSpec.CoreTablePath_;
    }
}


////////////////////////////////////////////////////////////////////////////////

TString MergeSpec(TNode& dst, const TOperationOptions& options)
{
    MergeNodes(dst["spec"], TConfig::Get()->Spec);
    if (options.Spec_) {
        MergeNodes(dst["spec"], *options.Spec_);
    }
    return NodeToYsonString(dst, YF_BINARY);
}

template <typename TSpec>
void CreateDebugOutputTables(const TSpec& spec, const TAuth& auth)
{
    if (spec.StderrTablePath_.Defined()) {
        Create(auth, TTransactionId(), *spec.StderrTablePath_, "table", true, true);
    }
    if (spec.CoreTablePath_.Defined()) {
        Create(auth, TTransactionId(), *spec.CoreTablePath_, "table", true, true);
    }
}

void CreateOutputTable(
    const TAuth& auth,
    const TTransactionId& transactionId,
    const TRichYPath& path)
{
    if (!path.Path_) {
        ythrow yexception() << "Output table is not set";
    }
    Create(auth, transactionId, path.Path_, "table", true, true);
}

void CreateOutputTables(
    const TAuth& auth,
    const TTransactionId& transactionId,
    const yvector<TRichYPath>& paths)
{
    if (paths.empty()) {
        ythrow yexception() << "Output tables are not set";
    }
    for (auto& path : paths) {
        CreateOutputTable(auth, transactionId, path);
    }
}

void LogJob(const TOperationId& opId, IJob* job, const char* type)
{
    if (job) {
        LOG_INFO("Operation %s; %s = %s",
            ~GetGuidAsString(opId), type, ~TJobFactory::Get()->GetJobName(job));
    }
}

TString DumpYPath(const TRichYPath& path)
{
    TStringStream stream;
    TYsonWriter writer(&stream, YF_TEXT, YT_NODE);
    Serialize(path, &writer);
    return stream.Str();
}

void LogYPaths(const TOperationId& opId, const yvector<TRichYPath>& paths, const char* type)
{
    for (size_t i = 0; i < paths.size(); ++i) {
        LOG_INFO("Operation %s; %s[%" PRISZT "] = %s",
            ~GetGuidAsString(opId), type, i, ~DumpYPath(paths[i]));
    }
}

void LogYPath(const TOperationId& opId, const TRichYPath& output, const char* type)
{
    LOG_INFO("Operation %s; %s = %s",
        ~GetGuidAsString(opId), type, ~DumpYPath(output));
}

} // namespace

////////////////////////////////////////////////////////////////////////////////

TOperationId ExecuteMap(
    const TAuth& auth,
    const TTransactionId& transactionId,
    const TMapOperationSpec& spec,
    IJob* mapper,
    const TOperationOptions& options)
{
    auto inputs = CanonizePaths(auth, spec.Inputs_);
    auto outputs = CanonizePaths(auth, spec.Outputs_);

    TMaybe<TNode> format;
    if (spec.InputDesc_.Format == TMultiFormatDesc::F_YAMR &&
        options.UseTableFormats_)
    {
        format = GetTableFormats(auth, transactionId, inputs);
    }

    CreateDebugOutputTables(spec, auth);
    CreateOutputTables(auth, transactionId, outputs);

    TJobPreparer map(
        auth,
        transactionId,
        "--yt-map",
        spec.MapperSpec_,
        mapper,
        outputs.size(),
        spec.InputDesc_,
        spec.OutputDesc_,
        options);

    TNode specNode = BuildYsonNodeFluently()
    .BeginMap().Item("spec").BeginMap()
        .Item("mapper").DoMap(std::bind(
            BuildUserJobFluently,
            std::cref(map),
            format,
            spec.InputDesc_,
            spec.OutputDesc_,
            std::placeholders::_1))
        .Item("input_table_paths").List(inputs)
        .Item("output_table_paths").List(outputs)
        .Item("job_io").BeginMap()
            .Item("control_attributes").BeginMap()
                .Item("enable_row_index").Value(true)
            .EndMap()
            .DoIf(!TConfig::Get()->TableWriter.Empty(), [&] (TFluentMap fluent) {
                fluent.Item("table_writer").Value(TConfig::Get()->TableWriter);
            })
        .EndMap()
        .DoIf(spec.Ordered_.Defined(), [&] (TFluentMap fluent) {
            fluent.Item("ordered").Value(spec.Ordered_.GetRef());
        })
        .Item("title").Value(map.GetClassName())
        .Do(std::bind(BuildCommonOperationPart, options, std::placeholders::_1))
    .EndMap().EndMap();

    BuildCommonUserOperationPart(spec, &specNode["spec"]);

    auto operationId = StartOperation(
        auth,
        transactionId,
        "map",
        MergeSpec(specNode, options));

    LogJob(operationId, mapper, "mapper");
    LogYPaths(operationId, inputs, "input");
    LogYPaths(operationId, outputs, "output");

    if (options.Wait_) {
        WaitForOperation(auth, transactionId, operationId);
    }
    return operationId;
}

TOperationId ExecuteReduce(
    const TAuth& auth,
    const TTransactionId& transactionId,
    const TReduceOperationSpec& spec,
    IJob* reducer,
    const TOperationOptions& options)
{
    auto inputs = CanonizePaths(auth, spec.Inputs_);
    auto outputs = CanonizePaths(auth, spec.Outputs_);

    TMaybe<TNode> format;
    if (spec.InputDesc_.Format == TMultiFormatDesc::F_YAMR &&
        options.UseTableFormats_)
    {
        format = GetTableFormats(auth, transactionId, inputs);
    }

    CreateDebugOutputTables(spec, auth);
    CreateOutputTables(auth, transactionId, outputs);

    TJobPreparer reduce(
        auth,
        transactionId,
        "--yt-reduce",
        spec.ReducerSpec_,
        reducer,
        outputs.size(),
        spec.InputDesc_,
        spec.OutputDesc_,
        options);

    TNode specNode = BuildYsonNodeFluently()
    .BeginMap().Item("spec").BeginMap()
        .Item("reducer").DoMap(std::bind(
            BuildUserJobFluently,
            std::cref(reduce),
            format,
            spec.InputDesc_,
            spec.OutputDesc_,
            std::placeholders::_1))
        .Item("sort_by").Value(spec.SortBy_)
        .Item("reduce_by").Value(spec.ReduceBy_)
        .DoIf(spec.JoinBy_.Defined(), [&] (TFluentMap fluent) {
            fluent.Item("join_by").Value(spec.JoinBy_.GetRef());
        })
        .Item("input_table_paths").List(inputs)
        .Item("output_table_paths").List(outputs)
        .Item("job_io").BeginMap()
            .Item("control_attributes").BeginMap()
                .Item("enable_key_switch").Value(true)
                .Item("enable_row_index").Value(true)
            .EndMap()
            .DoIf(!TConfig::Get()->TableWriter.Empty(), [&] (TFluentMap fluent) {
                fluent.Item("table_writer").Value(TConfig::Get()->TableWriter);
            })
        .EndMap()
        .Item("title").Value(reduce.GetClassName())
        .Do(std::bind(BuildCommonOperationPart, options, std::placeholders::_1))
    .EndMap().EndMap();

    BuildCommonUserOperationPart(spec, &specNode["spec"]);

    auto operationId = StartOperation(
        auth,
        transactionId,
        "reduce",
        MergeSpec(specNode, options));

    LogJob(operationId, reducer, "reducer");
    LogYPaths(operationId, inputs, "input");
    LogYPaths(operationId, outputs, "output");

    if (options.Wait_) {
        WaitForOperation(auth, transactionId, operationId);
    }
    return operationId;
}

TOperationId ExecuteJoinReduce(
    const TAuth& auth,
    const TTransactionId& transactionId,
    const TJoinReduceOperationSpec& spec,
    IJob* reducer,
    const TOperationOptions& options)
{
    auto inputs = CanonizePaths(auth, spec.Inputs_);
    auto outputs = CanonizePaths(auth, spec.Outputs_);

    TMaybe<TNode> format;
    if (spec.InputDesc_.Format == TMultiFormatDesc::F_YAMR &&
        options.UseTableFormats_)
    {
        format = GetTableFormats(auth, transactionId, inputs);
    }

    CreateDebugOutputTables(spec, auth);
    CreateOutputTables(auth, transactionId, outputs);

    TJobPreparer reduce(
        auth,
        transactionId,
        "--yt-reduce",
        spec.ReducerSpec_,
        reducer,
        outputs.size(),
        spec.InputDesc_,
        spec.OutputDesc_,
        options);

    TNode specNode = BuildYsonNodeFluently()
    .BeginMap().Item("spec").BeginMap()
        .Item("reducer").DoMap(std::bind(
            BuildUserJobFluently,
            std::cref(reduce),
            format,
            spec.InputDesc_,
            spec.OutputDesc_,
            std::placeholders::_1))
        .Item("join_by").Value(spec.JoinBy_)
        .Item("input_table_paths").List(inputs)
        .Item("output_table_paths").List(outputs)
        .Item("job_io").BeginMap()
            .Item("control_attributes").BeginMap()
                .Item("enable_key_switch").Value(true)
                .Item("enable_row_index").Value(true)
            .EndMap()
            .DoIf(!TConfig::Get()->TableWriter.Empty(), [&] (TFluentMap fluent) {
                fluent.Item("table_writer").Value(TConfig::Get()->TableWriter);
            })
        .EndMap()
        .Item("title").Value(reduce.GetClassName())
        .Do(std::bind(BuildCommonOperationPart, options, std::placeholders::_1))
    .EndMap().EndMap();

    BuildCommonUserOperationPart(spec, &specNode["spec"]);

    auto operationId = StartOperation(
        auth,
        transactionId,
        "join_reduce",
        MergeSpec(specNode, options));

    LogJob(operationId, reducer, "reducer");
    LogYPaths(operationId, inputs, "input");
    LogYPaths(operationId, outputs, "output");

    if (options.Wait_) {
        WaitForOperation(auth, transactionId, operationId);
    }
    return operationId;
}

TOperationId ExecuteMapReduce(
    const TAuth& auth,
    const TTransactionId& transactionId,
    const TMapReduceOperationSpec& spec,
    IJob* mapper,
    IJob* reduceCombiner,
    IJob* reducer,
    const TMultiFormatDesc& mapperClassOutputDesc,
    const TMultiFormatDesc& reduceCombinerClassInputDesc,
    const TMultiFormatDesc& reduceCombinerClassOutputDesc,
    const TMultiFormatDesc& reducerClassInputDesc,
    const TOperationOptions& options)
{
    auto inputs = CanonizePaths(auth, spec.Inputs_);
    auto outputs = CanonizePaths(auth, spec.Outputs_);

    TMaybe<TNode> format;
    if (spec.InputDesc_.Format == TMultiFormatDesc::F_YAMR &&
        options.UseTableFormats_)
    {
        format = GetTableFormats(auth, transactionId, inputs);
    }

    CreateDebugOutputTables(spec, auth);
    CreateOutputTables(auth, transactionId, outputs);

    TKeyColumns sortBy(spec.SortBy_);
    TKeyColumns reduceBy(spec.ReduceBy_);

    if (sortBy.Parts_.empty()) {
        sortBy = reduceBy;
    }

    if (spec.InputDesc_.Format == TMultiFormatDesc::F_YAMR && format && !mapper) {
        auto& attrs = format.Get()->Attributes();
       auto& keyColumns = attrs["key_column_names"].AsList();

        sortBy.Parts_.clear();
        reduceBy.Parts_.clear();

        for (auto& column : keyColumns) {
            sortBy.Parts_.push_back(column.AsString());
            reduceBy.Parts_.push_back(column.AsString());
        }

        if (attrs.HasKey("subkey_column_names")) {
            auto& subkeyColumns = attrs["subkey_column_names"].AsList();
            for (auto& column : subkeyColumns) {
                sortBy.Parts_.push_back(column.AsString());
            }
        }
    }

    const auto& reduceOutputDesc = spec.OutputDesc_;

    auto reduceInputDesc = MergeIntermediateDesc(reducerClassInputDesc, spec.ReduceInputHintDesc_,
        "spec from reducer CLASS input", "spec from HINT for reduce input");

    auto reduceCombinerOutputDesc = MergeIntermediateDesc(reduceCombinerClassOutputDesc, spec.ReduceCombinerOutputHintDesc_,
        "spec derived from reduce combiner CLASS output", "spec from HINT for reduce combiner output");

    auto reduceCombinerInputDesc = MergeIntermediateDesc(reduceCombinerClassInputDesc, spec.ReduceCombinerInputHintDesc_,
        "spec from reduce combiner CLASS input", "spec from HINT for reduce combiner input");

    auto mapOutputDesc = MergeIntermediateDesc(mapperClassOutputDesc, spec.MapOutputHintDesc_,
        "spec from mapper CLASS output", "spec from HINT for map output");

    const auto& mapInputDesc = spec.InputDesc_;

    const bool hasMapper = mapper != nullptr;
    const bool hasCombiner = reduceCombiner != nullptr;

    if (!hasMapper) {
        //request identity desc only for no mapper cases
        const auto& identityMapInputDesc = IdentityDesc(mapInputDesc);
        if (hasCombiner) {
            reduceCombinerInputDesc = MergeIntermediateDesc(reduceCombinerInputDesc, identityMapInputDesc,
                "spec derived from reduce combiner CLASS input", "identity spec from mapper CLASS input");
        } else {
            reduceInputDesc = MergeIntermediateDesc(reduceInputDesc, identityMapInputDesc,
                "spec derived from reduce CLASS input", "identity spec from mapper CLASS input" );
        }
    }

    TJobPreparer reduce(
        auth,
        transactionId,
        "--yt-reduce",
        spec.ReducerSpec_,
        reducer,
        outputs.size(),
        reduceInputDesc,
        reduceOutputDesc,
        options);

    TString title;

    TNode specNode = BuildYsonNodeFluently()
    .BeginMap().Item("spec").BeginMap()
        .DoIf(hasMapper, [&] (TFluentMap fluent) {
            TJobPreparer map(
                auth,
                transactionId,
                "--yt-map",
                spec.MapperSpec_,
                mapper,
                1,
                mapInputDesc,
                mapOutputDesc,
                options);

            fluent.Item("mapper").DoMap(std::bind(
                BuildUserJobFluently,
                std::cref(map),
                format,
                mapInputDesc,
                mapOutputDesc,
                std::placeholders::_1));

            title = "mapper:" + map.GetClassName() + " ";
        })
        .DoIf(hasCombiner, [&] (TFluentMap fluent) {
            TJobPreparer combine(
                auth,
                transactionId,
                "--yt-reduce",
                spec.ReduceCombinerSpec_,
                reduceCombiner,
                1,
                reduceCombinerInputDesc,
                reduceCombinerOutputDesc,
                options);

            fluent.Item("reduce_combiner").DoMap(std::bind(
                BuildUserJobFluently,
                std::cref(combine),
                mapper ? TMaybe<TNode>() : format,
                reduceCombinerInputDesc,
                reduceCombinerOutputDesc,
                std::placeholders::_1));
            title += "combiner:" + combine.GetClassName() + " ";
        })
        .Item("reducer").DoMap(std::bind(
            BuildUserJobFluently,
            std::cref(reduce),
            (mapper || reduceCombiner) ? TMaybe<TNode>() : format,
            reduceInputDesc,
            reduceOutputDesc,
            std::placeholders::_1))
        .Item("sort_by").Value(sortBy)
        .Item("reduce_by").Value(reduceBy)
        .Item("input_table_paths").List(inputs)
        .Item("output_table_paths").List(outputs)
        .Item("map_job_io").BeginMap()
            .Item("control_attributes").BeginMap()
                .Item("enable_row_index").Value(true)
            .EndMap()
            .DoIf(!TConfig::Get()->TableWriter.Empty(), [&] (TFluentMap fluent) {
                fluent.Item("table_writer").Value(TConfig::Get()->TableWriter);
            })
        .EndMap()
        .Item("sort_job_io").BeginMap()
            .Item("control_attributes").BeginMap()
                .Item("enable_key_switch").Value(true)
            .EndMap()
            .DoIf(!TConfig::Get()->TableWriter.Empty(), [&] (TFluentMap fluent) {
                fluent.Item("table_writer").Value(TConfig::Get()->TableWriter);
            })
        .EndMap()
        .Item("reduce_job_io").BeginMap()
            .Item("control_attributes").BeginMap()
                .Item("enable_key_switch").Value(true)
            .EndMap()
            .DoIf(!TConfig::Get()->TableWriter.Empty(), [&] (TFluentMap fluent) {
                fluent.Item("table_writer").Value(TConfig::Get()->TableWriter);
            })
        .EndMap()
        .Item("title").Value(title + "reducer:" + reduce.GetClassName())
        .Do(std::bind(BuildCommonOperationPart, options, std::placeholders::_1))
    .EndMap().EndMap();

    BuildCommonUserOperationPart(spec, &specNode["spec"]);

    auto operationId = StartOperation(
        auth,
        transactionId,
        "map_reduce",
        MergeSpec(specNode, options));

    LogJob(operationId, mapper, "mapper");
    LogJob(operationId, reduceCombiner, "reduce_combiner");
    LogJob(operationId, reducer, "reducer");
    LogYPaths(operationId, inputs, "input");
    LogYPaths(operationId, outputs, "output");

    if (options.Wait_) {
        WaitForOperation(auth, transactionId, operationId);
    }
    return operationId;
}


TOperationId ExecuteSort(
    const TAuth& auth,
    const TTransactionId& transactionId,
    const TSortOperationSpec& spec,
    const TOperationOptions& options)
{
    auto inputs = CanonizePaths(auth, spec.Inputs_);
    auto output = CanonizePath(auth, spec.Output_);

    CreateOutputTable(auth, transactionId, output);

    TNode specNode = BuildYsonNodeFluently()
    .BeginMap().Item("spec").BeginMap()
        .Item("input_table_paths").List(inputs)
        .Item("output_table_path").Value(output)
        .Item("sort_by").Value(spec.SortBy_)
        .Do(std::bind(BuildCommonOperationPart, options, std::placeholders::_1))
    .EndMap().EndMap();

    auto operationId = StartOperation(
        auth,
        transactionId,
        "sort",
        MergeSpec(specNode, options));

    LogYPaths(operationId, inputs, "input");
    LogYPath(operationId, output, "output");

    if (options.Wait_) {
        WaitForOperation(auth, transactionId, operationId);
    }
    return operationId;
}

TOperationId ExecuteMerge(
    const TAuth& auth,
    const TTransactionId& transactionId,
    const TMergeOperationSpec& spec,
    const TOperationOptions& options)
{
    auto inputs = CanonizePaths(auth, spec.Inputs_);
    auto output = CanonizePath(auth, spec.Output_);

    CreateOutputTable(auth, transactionId, output);

    TNode specNode = BuildYsonNodeFluently()
    .BeginMap().Item("spec").BeginMap()
        .Item("input_table_paths").List(inputs)
        .Item("output_table_path").Value(output)
        .Item("mode").Value(ToString(spec.Mode_))
        .Item("combine_chunks").Value(spec.CombineChunks_)
        .Item("force_transform").Value(spec.ForceTransform_)
        .Item("merge_by").Value(spec.MergeBy_)
        .Do(std::bind(BuildCommonOperationPart, options, std::placeholders::_1))
    .EndMap().EndMap();

    auto operationId = StartOperation(
        auth,
        transactionId,
        "merge",
        MergeSpec(specNode, options));

    LogYPaths(operationId, inputs, "input");
    LogYPath(operationId, output, "output");

    if (options.Wait_) {
        WaitForOperation(auth, transactionId, operationId);
    }
    return operationId;
}

TOperationId ExecuteErase(
    const TAuth& auth,
    const TTransactionId& transactionId,
    const TEraseOperationSpec& spec,
    const TOperationOptions& options)
{
    auto tablePath = CanonizePath(auth, spec.TablePath_);

    TNode specNode = BuildYsonNodeFluently()
    .BeginMap().Item("spec").BeginMap()
        .Item("table_path").Value(tablePath)
        .Item("combine_chunks").Value(spec.CombineChunks_)
        .Do(std::bind(BuildCommonOperationPart, options, std::placeholders::_1))
    .EndMap().EndMap();

    auto operationId = StartOperation(
        auth,
        transactionId,
        "erase",
        MergeSpec(specNode, options));

    LogYPath(operationId, tablePath, "table_path");

    if (options.Wait_) {
        WaitForOperation(auth, transactionId, operationId);
    }
    return operationId;
}

////////////////////////////////////////////////////////////////////////////////

::TIntrusivePtr<INodeReaderImpl> CreateJobNodeReader()
{
    return new TNodeTableReader(::MakeIntrusive<TJobReader>(0));
}

::TIntrusivePtr<IYaMRReaderImpl> CreateJobYaMRReader()
{
    return new TYaMRTableReader(::MakeIntrusive<TJobReader>(0));
}

::TIntrusivePtr<IProtoReaderImpl> CreateJobProtoReader()
{
    if (TConfig::Get()->UseClientProtobuf) {
        return new TProtoTableReader(
            ::MakeIntrusive<TJobReader>(0),
            GetJobInputDescriptors());
    } else {
        return new TLenvalProtoTableReader(
            ::MakeIntrusive<TJobReader>(0),
            GetJobInputDescriptors());
    }
}

::TIntrusivePtr<INodeWriterImpl> CreateJobNodeWriter(size_t outputTableCount)
{
    return new TNodeTableWriter(MakeHolder<TJobWriter>(outputTableCount));
}

::TIntrusivePtr<IYaMRWriterImpl> CreateJobYaMRWriter(size_t outputTableCount)
{
    return new TYaMRTableWriter(MakeHolder<TJobWriter>(outputTableCount));
}

::TIntrusivePtr<IProtoWriterImpl> CreateJobProtoWriter(size_t outputTableCount)
{
    if (TConfig::Get()->UseClientProtobuf) {
        return new TProtoTableWriter(
            MakeHolder<TJobWriter>(outputTableCount),
            GetJobOutputDescriptors());
    } else {
        return new TLenvalProtoTableWriter(
            MakeHolder<TJobWriter>(outputTableCount),
            GetJobOutputDescriptors());
    }
}

////////////////////////////////////////////////////////////////////////////////

} // namespace NYT
