# Системные процессы

В данном разделе собрана информация о системных процессах, которые регулярно запускаются на кластерах.

{% note warning "Внимание" %}

Конкретные настройки разных скриптов на кластерах могут отличаться. На данной странице описаны только типичные сценарии.

{% endnote %}

## Очистка списка операций  { #operations_cleaning }

Типичная частота запуска скрипта: раз в десять минут.

Периодически запускается процесс, который удаляет из [Кипариса](cypress.md) старые операции. Приоритет на удаление отдается успешно закончившимся операциями, у которых отсутствует `stderr`. Кроме того скрипт сохраняет  несколько последних операций для каждого пользователя.

## Очистка директории //tmp { #tmp_cleaning }

Типичная частота запуска скрипта: один раз в час.

Скрипт состоит из трех частей:

1. Очистка директории `//tmp` от объектов принадлежащих аккаунту `tmp`;
2. Очистка директории`//tmp/yt_wrapper/file_storage`;
3. Очистка директории `//tmp/trash`, в которой хранятся удаленные объекты.

Скрипт очистки составляет список объектов ([таблиц](objects.md#tables)/[файлов](objects.md#files)/[ссылок](objects.md#links)), упорядоченный по дате. Далее происходит обход списка и по каждому объекту принимается решение об удалении.

Критерии для удаления:

- Количество сохраненных объектов превысило некоторый порог (зависит от кластера);
- Общий `disk_space`, `chunk_count` или `node_count` сохраняемых объектов превысил половину [квоты аккаунта](quotas.md) `tmp` либо некоторые указанные пороги;
- В `map_node`, где лежит объект, накопилось много сохраненных объектов (более 40 000);
- `modification_time` объекта слишком старый (старше недели).

При очистке `file_storage` специально не удаляются объекты, у которых последнее изменение было менее десяти минут назад.

После удаления объектов происходит удаление пустых директорий.

В случае, если вы строите production-процесс полагаться на `//tmp` ненадежно, поэтому в клиентских API обычно есть возможность указать путь до tmp-директории. При этом возникает проблема очистки вашей личной tmp-директории, чтобы всем не приходилось самостоятельно изобретать процессы для такой очистки, можно воспользовать скриптом который чистит общественный `//tmp`. Данный скрипт живет [в аркадии](https://a.yandex-team.ru/arc/trunk/arcadia/yt/cron/clear_tmp), также можно воспользоваться уже готовым [sandbox scheduler-ом](https://sandbox.yandex-team.ru/scheduler/18724), донастроив его под себя.

## Перенос объектов в директорию //home из аккаунта tmp в пользовательский аккаунт { #home_tmp_move }

Типичная частота запуска скрипта: один раз в час.

В рамках работы скрипта в директории `//home` находятся все объекты с аккаунтом **tmp** и переносятся в ближайший (по иерархии пути объекта) пользовательский аккаунт.

## Merge таблиц с маленькими чанками { #nightly_merge }

Типичная частота запуска скрипта: один раз в день.

В рамках работы скрипта для статических таблиц, у которых средний размер чанка менее 512 Мб, запускается [операция объединения](../mr/merge.md).

Если на пути установлен атрибут `suppress_nightly_merge = true`, то скрипт не будет объединять чанки данной таблицы.

{% note warning "Внимание" %}

Данный процесс не дает никаких гарантий на то, что над определенными таблицами будет когда-либо запущена операция объединения. Основная задача данного процесса — поддержание общего числа чанков на всем кластере на приемлемом уровне.

{% endnote %}

{% note info "Примечание" %}

Данный процесс можно запустить для пользовательских данных. Для этого есть задача в sandbox: `YT_MERGE_TABLES_TASK`

У таска есть следующие опции
+ обязательные
  + `proxy` - прокси на котором запустить операцию
  + `root` - корень для поиска
  + `pool` - YT pool для выполнения операций
  + `queue` - `list_node` в Кипарисе для поддержания очереди операций (должен существовать)
  + `secret` - секрет из yav
  + `secret_key` - ключ в секрете с токеном от yt
+ опциональные
  + `minimum_number_of_chunks` - минимальное количество чанков в таблице, для занесения её в список
  + `thread_count` - количество параллельных операции
  + `fast_pool` - YT pool для выполнения быстрых операций
  + `tmp` - `map_node` в Кипарисе для промежуточных результатов, по умолчанию случайная папка в `//tmp`
+ дефолтные (лучше не менять)
  + `ignore_acl` - флаг "не сохранять явный acl"

Чтобы создать `list_node`, нужно выполнить команду `yt create list_node //home/path/to/queue`

{% endnote %}

## Пережатие таблиц { #nightly_compress }

Типичная частота запуска скрипта: один раз в четыре часа.

Периодически запускается процесс, который находит все таблицы (поиск происходит от корня [Кипариса](cypress.md)), у которых проставлен атрибут `force_nightly_compress` и запускает для них пережатие в erasure и gzip. При этом указанный атрибут снимается.

{% note info "Примечание" %}

Существует возможность сжимать таблицы внутри директорий автоматически.

{% endnote %}

Для автоматического сжатия нужно выставить на директории атрибут `nightly_compression_settings` со следующими полями:
  - `enabled` (`bool`, по умолчанию отсутсвует) &mdash; включать ли компрессиию
    на директории, `%true` включает, `%false` полностью отключает, если значение отсутствует,
    то будут пережаты только таблицы с атрибутом `force_nightly_compress=%true`;
  - `pool` (`str`, по умолчанию `cron_compression`) &mdash; вычислительный пул, который будет
    использован для операций пережатия, **внимание:** _перед использованием нового пула
    в скриптах сжатия, необходимо завести одноименную очередь_;
  - `compression_codec` (`str`, по умолчанию `zlib_9`) &mdash; кодек сжатия, в который необходимо
    пережимать таблицы;
  - `erasure_codec` (`str`, по умолчанию `lrc_12_2_2`) &mdash; erasure кодек, в который необходимо
    пережимать таблицы, (если erasure кодирование не нужно, можно указать кодек `none`);
  - `min_table_size` (`int`, по умолчанию `0`) &mdash; минимальный размер таблиц (в байтах,
    сравнивается с `@uncompressed_data_size`), которые необходимо пережимать;
  - `min_table_age` (`int`, по умолчанию `0`) &mdash; минимальный возраст таблиц (в секундах,
    сравнивается с `@creation_time` или с `@nightly_compression_user_time`, если пользователем указан данный атрибут);
  - `owners` (`list[str]`, по умолчанию `[]`) &mdash; список пользователей, которые будут прописаны
    во владельцы операций, эти пользователи смогут управлять этими операциями, например абортить их;
  - `desired_chunk_size` (`int`, по умолчанию отсутствует) &mdash; размер чанка, в который будет
    целиться сжатие;
  - `force_recompress_to_specified_codecs` (`bool`, по умолчанию `false`) &mdash; пережимать ли таблицы, которые уже были один раз сжаты, если `false`, то единожды сжатые таблицы не будут никогда пережиматься, если `true`, то сжатые таблицы будут пережиматься, если в них появились новые данные;
  - `optimize_for` (`str`, по умолчанию отсутствует) &mdash; формат хранения чанков, возможные значения `lookup` и `scan`.

При этом сжиматься могут и динамические таблицы (с конвертацией в статичиские). Для этого, помимо настроек описанных выше:
- динамическая таблица должна быть отмонтирована или заморожена.
- у динамической таблицы должен быть выставлен атрибут `@nightly_compression_select_timestamp` (будет использоваться для чтения данных)
- или этот атрибут может быть выставлен в настройках на директорию - `@nightly_compression_settings/dynamic_table_select_timestamp` (с этим тэймстампом будет сжиматься все вложенные динамические таблицы)

Пример конфига:
```bash
{
    "enabled" = %true;
    "pool" = "my_team_compression_pool_name"
    "compression_codec" = "brotli_9";
    "erasure_codec" = "lrc_12_2_2";
}
```

Пример команды, устанавливающей конфиг:

```
yt set //home/mydir/@nightly_compression_settings '{enabled=%true;pool=my_compression_pool}'
```

Точную логику того, как выбираются таблицы для пережатия и как соотносятся указанные значения
атрибутов на вложенных путях можно изучить
[прямо в коде](https://a.yandex-team.ru/arc/trunk/arcadia/yt/cron/compression/compressionlib/collectorlib.py).

{% note info "Примечание" %}

Для того, чтобы выполнялось пережатие, нужно написать на рассылку [yt-admin@](https://ml.yandex-team.ru/lists/yt-admin/) с просьбой завести очередь на сжатие. Название очереди должно совпадать с названием пула.

{% endnote %}

{% note warning "Внимание" %}

В текущей версии, если не указать `pool`, сжатие будет происходить в общем пуле `cron`.
Данная функциональность скоро будет отключена, поэтому обязательно указывайте свой `pool`.

{% endnote %}

Вся информация о задачах пережатия хранится в нескольких статических таблицах YT.
Каждая таблица считается отдельной очередью и обслуживается параллельно фиксированным
количеством рабочих процессов. Название таблицы берется из названия пула
(настройка `pool` в `nightly_compression_settings`, если такая очередь не заведена,
то сжатие работать не будет). За создание таблицы отвечают администраторы,
они же регулируют параллельность. Если в настройках указан  `pool`,
который администратором не заведен, то пережатие не выполняется.

{% note info "Примечание" %}

Пользователь может следить за процессом, для этого нужно написать на рассылку [yt-admin@](https://ml.yandex-team.ru/lists/yt-admin/) и указать `pool`, который будет использоваться для пережатия.

{% endnote %}

Для каждой заведенной администратором очереди с системе монитринга Solomon можно найти [графики](https://solomon.yandex-team.ru/?project=yt&cluster=hahn&service=nightly_compression&queue=cron_compression&graph=auto&autorefresh=y&stack=false&b=1d&e=).

Для просмотра списка существующих очередей можно выполнить команду:

```bash
yt list //sys/cron/compression/queues --proxy <cluster_name>
```

#### Заведение новой очереди { #nightly_compress_queue }

Для заведений новой очереди необходимо на кластере создать таблицу в директории
`//sys/cron/compression/queues` с такой же схемой, как и у
`//sys/cron/compression/queues/cron_compression`.  В атрибуте `worker_count` у таблицы
необходимо указать количество worker-ов, которые будут запускать операции пережатия.
Хорошим значением для начала будет  `worker_count = 10`.
Например.

```python
YT_PROXY=hahn
python -c 'import yt.wrapper as yt; yt.create("table", "//sys/cron/compression/queues/voice", attributes={"schema": yt.get("//sys/cron/compression/queues/cron_compression/@schema"), "worker_count": 10})'
```

## Удаление пустых таблиц и директорий { #prune_empty }

Типичная частота запуска скрипта: один раз в день.

Периодически запускается процесс, который удаляет пустые таблицы и директории. Для его включения нужно установить на директорию опции `prune_empty_tables` и `prune_empty_map_nodes` соответственно (опция рекурсивная, но может быть отключена на поддиректории путем установки на нее `prune_empty_* = %false` ).