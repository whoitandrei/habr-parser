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

## Очереди

- `raw_articles` — `fetch_id`, `source_url`, `raw_html`, `fetched_at`, `trace_id`;
- `parsed_articles` — тот же `trace_id` + массив статей со счётчиками;
- `digest_ready` —  каждый подписчик привязывает свою durable-очередь
  (`digest_ready.notifier`, `digest_ready.web`).

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

## Запуск

```bash
docker compose up --build
```

- **Веб-морда:** <http://localhost:8080> (обновляется сама раз в 30 с).
  Ещё есть `/api/digest` и `/healthz`.
- **RabbitMQ UI:** <http://localhost:15672>, `guest` / `guest`.
- **Логи:** `docker compose logs -f fetcher parser storage notifier web`.
- **Дайджест файлом:** `docker compose exec notifier cat /data/digests/latest.md`.

## Локальная сборка без Docker

```bash
conan install . --build=missing -s build_type=Release -o "*:shared=False" --output-folder=build
cmake -B build -S . -DCMAKE_TOOLCHAIN_FILE=build/conan_toolchain.cmake -DCMAKE_BUILD_TYPE=Release
cmake --build build -j
```

## Конфигурация

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
