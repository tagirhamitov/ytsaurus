package ru.yandex.yt.ytclient.rpc;

import ru.yandex.yt.ytclient.rpc.internal.Compression;

import java.util.List;
import java.util.concurrent.CompletableFuture;

public interface RpcClientStreamControl extends RpcClientRequestControl {
    void subscribe(RpcStreamConsumer consumer);

    Compression compression();

    CompletableFuture<Void> feedback(long offset);
    CompletableFuture<Void> sendEof();

    CompletableFuture<Void> sendPayload(List<byte[]> attachments);

    void wakeUp();
}
