# habr-digest — микросервисный конвейер на C++

Учебный пет-проект: пять сервисов собирают популярные статьи с Хабра и готовят
из них дайджест. Общаются только через RabbitMQ, сообщения в JSON.

```
fetcher ──raw_articles──▶ parser ──parsed_articles──▶ storage
                                                          │
                                            digest_ready (fanout exchange)
                                                    ╱           ╲
                                               notifier          web
                                             (файл + лог)   (страница на :8080)
```

## Идея и цель

Каждый слой написан руками, без фреймворков:

- **event loop** — один `event_base` (libevent) на процесс: на нём AMQP-сокет,
  таймер и обработчики SIGTERM/SIGINT;
- **AMQP** — AMQP-CPP на низком уровне: свой реконнект, ручные ack/reject,
  durable-очереди и persistent-сообщения;
- **HTTP** — `evhttp` из той же libevent, поэтому веб-сервис остался
  однопоточным и без мьютексов;
- **парсинг HTML** — libxml2 + XPath, без регулярок;
- **хранилище** — SQLite: prepared statements, транзакции, дедупликация через
  `ON CONFLICT DO UPDATE`;
- **контракты** — общие структуры + `NLOHMANN_DEFINE_TYPE_INTRUSIVE`, одни и те
  же у продюсера и консьюмера.

## Стек

- **Язык:** C++17.
- **Сборка:** CMake ≥ 3.16 + Conan 2.x (`CMakeToolchain`/`CMakeDeps`).
- **Зависимости:** amqp-cpp, libevent, nlohmann_json, libcurl, libxml2, sqlite3.
- **Инфраструктура:** Docker Compose, RabbitMQ 3.13-management.
- **Образы:** multi-stage, `ubuntu:22.04` → статическая линковка → рантайм от
  непривилегированного пользователя.
- **Платформа:** Linux в контейнерах, разработка на macOS (arm64).

## Сервисы

| Сервис | Что делает | Своя зависимость |
|---|---|---|
| `fetcher` | По таймеру скачивает листинг Хабра, публикует сырой HTML | libcurl |
| `parser` | Разбирает HTML в структурированные статьи | libxml2 |
| `storage` | Хранит статьи, по своему таймеру отбирает топ-N и публикует дайджест | sqlite3 |
| `notifier` | Рендерит дайджест в Markdown, пишет файл и лог | — |
| `web` | Показывает последний дайджест на HTTP-странице | libevent (`evhttp`) |

Отдельных `scheduler` и `ranking` нет: таймер — это несколько строк на libevent,
а ранжирование — один `ORDER BY` по таблице, которой владеет только `storage`.

## Очереди

- `raw_articles` — `fetch_id`, `source_url`, `raw_html`, `fetched_at`, `trace_id`;
- `parsed_articles` — тот же `trace_id` + массив статей со счётчиками;
- `digest_ready` — **fanout-обменник**, а не очередь: очередь отдала бы каждый
  дайджест только одному из подписчиков, и `notifier` с `web` делили бы их
  пополам. Каждый подписчик привязывает свою durable-очередь
  (`digest_ready.notifier`, `digest_ready.web`).

`trace_id` генерируется в `fetcher` на цикл «скачали → распарсили → сохранили» и
копируется дальше без изменений. У дайджеста свой `trace_id` — он агрегирует
много циклов; исходный сохранён в `source_trace_id` каждой статьи.

Схемы: [`shared/cpp/messaging/include/messaging/messages.hpp`](shared/cpp/messaging/include/messaging/messages.hpp).

## Структура проекта

```
habr-parser/
├── CMakeLists.txt      сборка всего монорепо разом (для локальной разработки)
├── conanfile.txt       объединение зависимостей всех сервисов
├── docker-compose.yml
├── shared/cpp/
│   ├── common/         логирование, ISO-таймстемпы, trace_id
│   └── messaging/      AmqpService (event loop, реконнект, таймер, сигналы) + схемы
└── services/           fetcher/ parser/ storage/ notifier/ web/
                        у каждого свои CMakeLists.txt, conanfile.txt, Dockerfile
```

Неймспейс зеркалит путь: `habr::services::fetcher`, `habr::shared::messaging`.

## Запуск

```bash
docker compose up --build
```

Первая сборка — несколько минут: Conan собирает зависимости. Кеш Conan общий для
всех образов (BuildKit cache mount), поэтому OpenSSL компилируется один раз, а
не пять.

- **Веб-морда:** <http://localhost:8080> (обновляется сама раз в 30 с).
  Ещё есть `/api/digest` и `/healthz`.
- **RabbitMQ UI:** <http://localhost:15672>, `guest` / `guest`.
- **Логи:** `docker compose logs -f fetcher parser storage notifier web`.
- **Дайджест файлом:** `docker compose exec notifier cat /data/digests/latest.md`.

Первый `fetch` происходит сразу при подключении к брокеру, первый дайджест —
через `DIGEST_INTERVAL_SECONDS`.

Graceful shutdown: `docker compose stop fetcher` → в логе `Received signal 15` →
`Shutting down gracefully` → `Stopped, exit_code=0`, без SIGKILL по таймауту.

## Локальная сборка без Docker

```bash
conan install . --build=missing -s build_type=Release -o "*:shared=False" --output-folder=build
cmake -B build -S . -DCMAKE_TOOLCHAIN_FILE=build/conan_toolchain.cmake -DCMAKE_BUILD_TYPE=Release
cmake --build build -j
```

## Конфигурация

Всё через переменные окружения, значения по умолчанию — в `config.hpp` каждого
сервиса.

- **все:** `RABBITMQ_HOST` (`localhost`), `RABBITMQ_PORT` (`5672`),
  `RABBITMQ_USER`/`RABBITMQ_PASSWORD` (`guest`), `RABBITMQ_VHOST` (`/`),
  `RABBITMQ_MAX_RECONNECT_ATTEMPTS` (`10`), `RABBITMQ_RECONNECT_DELAY_SECONDS` (`3`);
- **fetcher:** `HABR_URL`, `FETCH_INTERVAL_SECONDS` (`300`),
  `FETCH_TIMEOUT_SECONDS` (`10`), `HTTP_USER_AGENT`;
- **storage:** `DATABASE_PATH` (`/data/habr.db`), `DIGEST_INTERVAL_SECONDS`
  (`120`), `DIGEST_PERIOD_HOURS` (`24`), `DIGEST_TOP_N` (`10`),
  `DIGEST_WEIGHT_VOTES`/`_BOOKMARKS`/`_COMMENTS`/`_VIEWS`;
- **notifier:** `DIGEST_OUTPUT_DIR` (`/data/digests`);
- **web:** `HTTP_BIND` (`0.0.0.0`), `HTTP_PORT` (`8080`), `DIGEST_HISTORY_SIZE` (`20`).

Формула ранжирования: `score = votes*10 + bookmarks*5 + comments*2 + views*0.005`.
Вес просмотров маленький намеренно — их на два порядка больше, иначе сортировка
выродилась бы в сортировку по просмотрам.

## Решения

- **HTTP-запрос в event loop блокирующий** — `curl_easy_perform` останавливает
  `event_base` на время запроса. При одном GET раз в 5 минут асинхронный
  `curl_multi` поверх libevent не окупается.
- **Идемпотентности в `fetcher` нет** — он публикует страницу целиком и не знает
  про `article_id`. Дедупликация в `storage`, ключ — id статьи с Хабра.
- **SQLite, а не PostgreSQL** — один писатель, тысячи строк. Убирает контейнер и
  сетевой хоп, оставляет SQL; переезд на PostgreSQL — замена драйвера.
- **libxml2 + XPath, а не регулярки** — разметка Хабра это вложенный Vue SSR с
  маркерами `<!--[-->`.
- **`evhttp`, а не cpp-httplib** — libevent уже есть, и HTTP садится на тот же
  `event_base`: ни нового потока, ни мьютекса.
- **`web` ничего не хранит** — ни базы, ни общего volume, только то, что пришло
  из очереди. Иначе read-path пробил бы правило «только через RabbitMQ».
- **HTML экранируется** — заголовки приехали из чужого HTML, без экранирования
  это XSS через заголовок статьи.
- **`reject` без requeue для неразобранных сообщений** — на следующей попытке
  они разберутся ровно так же. Requeue только для ошибок записи (диск, брокер).
- **Ошибка сети не роняет процесс** — логируется, ждём следующего тика, без
  ретрая внутри тика.
- **OpenSSL объявлен прямой зависимостью** — публичные заголовки AMQP-CPP
  подключают `<openssl/ssl.h>`, но его рецепт не помечает их транзитивными.

Развёрнутые обоснования — в комментариях у самих мест решения.
