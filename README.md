# vAMF (C++): EricssonSoftware-like CLI Prototype

Прототип сетевого элемента AMF на C++ с поддержкой операторского CLI, иерархической конфигурации и механики candidate/running.

## 1. Текущее состояние

### Реализовано

- AMF state machine: `IDLE`, `INITIALIZING`, `RUNNING`, `DEGRADED`, `STOPPED`.
- UE-операции: register/deregister/find/list.
- PLMN по умолчанию: `MCC=250`, `MNC=03`.
- Иерархический CLI:
  - `AMF#` (exec)
  - `AMF(config)#`
  - `AMF(config-amf)#`
  - `AMF(config-amf-n2)#`
  - `AMF(config-amf-sbi)#`
- Candidate/running workflow:
  - `commit`
  - `commit confirmed <seconds>`
  - `confirm`
  - `rollback` / `discard`
- Multi-user candidate lock:
  - owner-id в рамках текущей CLI-сессии
  - роль сессии: operator или admin
  - lock с TTL
  - renew/unlock/force-unlock
  - блокировка изменений и commit для не-владельца lock
  - права задаются через внешнюю RBAC policy table

### Реализованные интерфейсы AMF

| Интерфейс | Куда подключен | Протокол | Назначение |
| --- | --- | --- | --- |
| N1 | UE | NAS (5G) | Сигнализация с UE |
| N2 | gNB / NG-RAN | N2AP | Управление радиоресурсами, paging |
| N3 | UPF | GTP-U | Пользовательский трафик (через UPF) |
| N8 | UDM | Service-Based | Получение данных подписки |
| N11 | SMF | Service-Based | Создание/управление PDU-сессиями |
| N12 | AUSF | Service-Based | Аутентификация |
| N14 | Другой AMF | Service-Based | Передача контекста при handover |
| N15 | PCF | Service-Based | Получение политики |
| N22 | NSSF | Service-Based | Выбор среза сети |
| N26 | MME (4G) | GTPv2-C | Interworking 4G-5G (handover, ISR) |

### Ограничения прототипа

- N1/N2/N3/N8/N11/N12/N14/N15/N22/N26 пока реализованы как моковые адаптеры (печать в консоль/лог), без реального сетевого стека.
- Модель multi-user симулируется в одном процессе CLI через смену owner-id команды.
- Конфигурация хранится в памяти процесса, без персистентного хранилища.

## 2. Структура проекта

- `CMakeLists.txt` - конфигурация сборки.
- `include/amf/interfaces.hpp` - интерфейсы и модели AMF.
- `include/amf/amf.hpp` - класс `AmfNode`.
- `include/amf/cli.hpp` - интерфейс CLI shell.
- `include/amf/app/amf_services.hpp` - application service слой (use-case API).
- `include/amf/adapters/console_adapters.hpp` - консольные адаптеры N1/N2/N3/N8/N11/N12/N14/N15/N22/N26/SBI.
- `include/amf/config/runtime_config.hpp` - модель runtime-конфига и загрузчик YAML/JSON.
- `include/amf/logging/file_logger.hpp` - файловый логгер.
- `include/amf/modules/registration.hpp` - модуль Registration.
- `include/amf/modules/mobility.hpp` - модуль Mobility.
- `include/amf/modules/control_plane.hpp` - модуль Control Plane (N1/N2/N8/N11/N12/N15/N22 + PLMN/SBI).
- `include/amf/modules/user_plane.hpp` - модуль User Plane (N3).
- `include/amf/modules/interworking.hpp` - модуль Interworking (N14/N26).
- `include/amf/modules/session_management.hpp` - legacy-модуль Session Management (для обратной совместимости в тестах).
- `src/amf.cpp` - реализация AMF-оркестратора и агрегатора модулей.
- `src/cli.cpp` - реализация CLI и конфиг-режимов.
- `src/app/amf_services.cpp` - реализация application service слоя.
- `src/adapters/console_adapters.cpp` - реализация консольных адаптеров N2/SBI.
- `src/config/runtime_config.cpp` - парсер runtime-конфига (.json/.yaml/.yml).
- `src/logging/file_logger.cpp` - реализация файлового логгера.
- `src/modules/registration.cpp` - реализация Registration.
- `src/modules/mobility.cpp` - реализация Mobility.
- `src/modules/control_plane.cpp` - реализация Control Plane.
- `src/modules/user_plane.cpp` - реализация User Plane.
- `src/modules/interworking.cpp` - реализация Interworking.
- `src/modules/session_management.cpp` - legacy-реализация Session Management.
- `src/main.cpp` - точка входа и консольные адаптеры N2/SBI.
- `tests/amf_tests.cpp` - unit/smoke тесты модулей и интеграционный тест `AmfNode`.
- `tests/cli_tests.cpp` - сценарные CLI-тесты для lock/TTL/RBAC.
- `tests/config_logging_tests.cpp` - unit-тесты парсинга YAML/JSON и записи логов.

## 3. Сборка и запуск (Windows, PowerShell)

```powershell
cmake -S . -B build
cmake --build build
.\build\Debug\amf.exe
```

Запуск с runtime-конфигом:

```powershell
.\build\Debug\amf.exe .\config\amf-config.yaml
# или
.\build\Debug\amf.exe .\config\amf-config.json
```

Запуск тестов:

```powershell
ctest --test-dir build --output-on-failure -C Debug
```

В этом наборе тестов теперь есть:

- `amf_tests`
- `amf_cli_tests`
- `amf_config_logging_tests`

## 4. Быстрый старт (конкуренция owner-id)

```text
AMF# conf t
AMF(config)# session owner opA
AMF(config)# candidate lock 10
AMF(config)# amf
AMF(config-amf)# mnc 04
AMF(config-amf)# exit
AMF(config)# session owner opB
AMF(config)# amf
AMF(config-amf)# mnc 05           <- reject (lock owner is opA)
AMF(config-amf)# exit
AMF(config)# show configuration lock
AMF(config)# session owner opA
AMF(config)# commit
AMF(config)# end
```

## 5. Команды CLI

### Exec mode (AMF#)

- `show session`
- `show policy`
- `session owner <owner-id>`
- `session role operator|admin`
- `policy reload`
- `show amf status`
- `show amf stats`
- `show amf ue [imsi]`
- `show running config`
- `show configuration candidate`
- `show configuration diff`
- `show configuration lock`
- `runtime-config reload [path]` (алиасы: `runtime_config`, `runtimeconfig`)
- `amf start|stop|degrade|recover|tick`
- `ue register <imsi> <tai>`
- `ue deregister <imsi>`
- `simulate n2 <imsi> <payload>`
- `simulate sbi <service> <payload>`
- `simulate n1 <imsi> <payload>`
- `simulate n3 <imsi> <payload>`
- `simulate n8 <imsi>`
- `simulate n11 <imsi> <operation>`
- `simulate n12 <imsi>`
- `simulate n14 <imsi> <target-amf>`
- `simulate n15 <imsi>`
- `simulate n22 <imsi> <snssai>`
- `simulate n26 <imsi> <operation>`
- `clear stats`
- `configure terminal` / `conf t`
- `exit|quit`

### Config mode (AMF(config)#)

- `show session`
- `show policy`
- `session owner <owner-id>`
- `session role operator|admin`
- `policy reload`
- `runtime-config reload [path]` (алиасы: `runtime_config`, `runtimeconfig`)
- `amf`
- `candidate lock <ttl-seconds>`
- `candidate renew <ttl-seconds>`
- `candidate unlock`
- `candidate force-unlock`
- `commit`
- `commit confirmed <seconds>`
- `confirm`
- `rollback|discard`
- `show running config`
- `show configuration candidate`
- `show configuration diff`
- `show configuration lock`
- `do <exec-command>`
- `end|exit`

### Config AMF mode (AMF(config-amf)#)

- `plmn <mcc(3)> <mnc(2)>`
- `mcc <value>`
- `mnc <value>`
- `n2`
- `sbi`
- `commit`
- `commit confirmed <seconds>`
- `confirm`
- `rollback|discard`
- `do <exec-command>`
- `end|exit`

### Config AMF N2 mode (AMF(config-amf-n2)#)

- `local-address <ipv4>`
- `port <1..65535>`
- `do <exec-command>`
- `end|exit`

### Config AMF SBI mode (AMF(config-amf-sbi)#)

- `bind-address <ipv4>`
- `port <1..65535>`
- `nf-instance <id>`
- `do <exec-command>`
- `end|exit`

## 6. Lock and TTL semantics

1. Owner-id задается командой `session owner <owner-id>`.
2. Роль задается командой `session role operator|admin`.
3. Права роли задаются в файле `rbac-policy.conf` без правки кода.
4. Candidate lock берется как `candidate lock <ttl-seconds>`.
5. Модификация candidate-конфига и commit доступны только владельцу lock.
6. `candidate renew <ttl-seconds>` продлевает lock только владельцу.
7. `candidate force-unlock` и commit зависят от policy table для текущей роли.
8. По истечении TTL lock автоматически снимается.

Пример policy table (`rbac-policy.conf`):

```text
role operator candidate_lock allow
role operator candidate_renew allow
role operator candidate_unlock allow
role operator rollback allow
role operator commit allow
role operator commit_confirmed allow
role operator confirm_commit allow
role operator force_unlock deny
role operator policy_reload deny
role operator session_role_change allow
role operator session_owner_change allow
role operator show_policy allow

role admin candidate_lock allow
role admin candidate_renew allow
role admin candidate_unlock allow
role admin rollback allow
role admin commit allow
role admin commit_confirmed allow
role admin confirm_commit allow
role admin force_unlock allow
role admin policy_reload allow
role admin session_role_change allow
role admin session_owner_change allow
role admin show_policy allow
```

После изменения файла применить без перезапуска:

```text
AMF# policy reload
AMF# show policy
```

## 7. Commit confirmed semantics

1. Выполняется `commit confirmed <seconds>`.
2. Изменение применяется сразу в running.
3. Если до истечения таймера не вызвать `confirm`, выполняется auto-rollback.

Пример:

```text
AMF(config)# commit confirmed 30
AMF(config)# confirm
```

## 8. Валидации

- `owner-id`: `[A-Za-z0-9_-]`, длина до 32.
- `role`: `operator` или `admin`.
- `mcc`: ровно 3 цифры.
- `mnc`: ровно 2 цифры.
- `local-address` / `bind-address`: IPv4.
- `port`: 1..65535.
- `nf-instance`: `[A-Za-z0-9_-]`, длина до 32.

## 9. Audit logging

CLI пишет аудит-события в файл `amf-audit.log` (в текущем рабочем каталоге процесса). Логируются:

- `candidate-lock`, `candidate-renew`, `candidate-unlock`, `candidate-force-unlock`
- `commit`, `commit-confirmed`, `confirm-commit`, `rollback`, `auto-rollback`
- `policy-reload`

Формат строки:

```text
<UTC timestamp> owner=<owner-id> role=<operator|admin> action=<action> result=<success|deny> detail="..."
```

## 10. Runtime config (YAML/JSON)

Поддерживаются форматы `.yaml`, `.yml`, `.json`.

Поддерживаемые поля:

- `mcc`, `mnc`
- `log_file`, `audit_log_file`, `rbac_policy_file`
- `n2.local_address`, `n2.port`
- `sbi.bind_address`, `sbi.port`, `sbi.nf_instance`

Примеры готовы в файлах:

- `config/amf-config.yaml`
- `config/amf-config.json`

## 11. File logging

Помимо `amf-audit.log`, добавлен общий процессный лог в файл `log_file` из runtime-конфига (по умолчанию `amf.log`).
В этот лог пишутся:

- старт/останов процесса
- применение runtime-конфига
- N2/SBI события из консольных адаптеров
