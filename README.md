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

### Матрица реализации (Now vs Target)

| Интерфейс | Реализация сейчас | 3GPP target | Gap |
| --- | --- | --- | --- |
| N1 | NAS5G codec + procedural FSM (`RegistrationRequest -> AuthenticationRequest -> SecurityModeCommand -> RegistrationAccept`) + per-UE security context (`K_AMF`, `RAND`, `AUTN`, UL/DL counters) | NAS (5G) message set + security context handling | Упрощенная crypto-модель и ограниченный набор NAS процедур |
| N2 | Structured NGAP-like codec with IE validation and procedures (`InitialUEMessage`, `InitialContextSetupRequest`, `UEContextReleaseCommand`, `Paging`) + per-UE NGAP context | Полный NGAP/N2AP (ASN.1 PER, IE validation, процедуры) | Нет ASN.1 PER и покрыта только часть NGAP процедур |
| N3 | Structured GTPU-like codec + tunnel lifecycle (`TunnelEstablish`, `UplinkTpdu`, `DownlinkTpdu`, `TunnelRelease`) + per-UE tunnel context (`TEID`, `QFI`, UL/DL sequence) + binary-header field emulation (`version/flags/type/length/seq`) | GTP-U encapsulation/decapsulation | Нет настоящего binary GTP-U encode/decode и ограниченный набор процедур |
| N8 | Structured N8SBI-like contract with procedures (`GetAmData`, `GetSmfSelectionData`, `GetUeContextInSmfData`) + per-UE subscription request context + IE validation/ErrorIndication | SBA API взаимодействие с UDM | Нет полного HTTP schema/model покрытия UDM и ограниченный набор процедур |
| N11 | Structured N11SBI-like contract with procedures (`Create`, `Modify`, `Release`) + per-UE PDU session state/context + IE/state validation/ErrorIndication | SBA API SMF (PDU session lifecycle) | Нет полного SMF API schema/response model и ограниченный набор процедур |
| N12 | Structured N12SBI-like auth contract (`AuthRequest`, `AuthResponse`) + per-UE auth context (`RAND`, `AUTN`, `XRES*`, state) + IE/state validation/ErrorIndication | SBA API AUSF authentication flow | Нет полного AKA/EAP и ограниченный набор процедур |
| N14 | Structured N14SBI-like contract with procedures (`PrepareHandover`, `ContextTransfer`, `CompleteTransfer`, `RollbackContext`) + per-UE handover transfer context (`target AMF`, `transfer-id`, `version`, `state`) + validation/ErrorIndication | SBA AMF context transfer | Нет полного AMF relocation schema/model и покрыта только часть handover context transfer flow |
| N15 | Structured N15SBI-like contract with procedures (`GetAmPolicy`, `GetSmPolicy`, `UpdatePolicyAssociation`) + per-UE policy association context + IE/state validation/ErrorIndication | SBA API PCF policy control | Нет полного PCF API schema/response model и ограниченный набор процедур |
| N22 | Structured N22SBI-like contract with procedures (`SelectSlice`, `UpdateSelection`, `ReleaseSelection`) + per-UE selection context (`selection-id`, `selected SNSSAI`, `allowed SNSSAI`, `state`) + fallback/validation/ErrorIndication | SBA API NSSF slice selection | Нет полного NSSF API schema/model и покрыта только часть selection rules |
| N26 | Structured GTPV2C-like codec with procedures (`HandoverRequest`, `ContextTransfer`, `IsrActivate`, `IsrDeactivate`, `ReleaseContext`) + per-UE MME context (`MME-TEID`, `ENB-TEID`, seq, state) + IE validation/ErrorIndication | GTPv2-C interworking with MME | Нет ASN.1/binary GTPv2-C encoding и ограниченный набор процедур |

Приоритеты развития (рекомендуемые):

1. N2/N1: процедурная сигнализация и корректные протокольные кодеки.
2. N3/N26: переход с payload-шаблонов на GTP-U/GTPv2-C.
3. N8/N11/N12/N14/N15/N22: унификация Service-Based интерфейсов на HTTP/SBA контракты по аналогии с текущим SBI stack.

### Roadmap by Interface

Фазы:

- `Phase 1`: Protocol foundation (кодеки/базовые on-wire форматы)
- `Phase 2`: Procedure coverage (ключевые 3GPP процедуры и state sync)
- `Phase 3`: Operational hardening (negative paths, interop, perf/telemetry)

Владельцы и ETA по фазам:

| Phase | Owner | ETA | Exit criteria |
| --- | --- | --- | --- |
| Phase 1 | AMF Core + Protocol Team | 2026-Q2 | Базовые on-wire контракты реализованы, сборка/тесты зеленые |
| Phase 2 | AMF Core + Mobility/Session Team | 2026-Q3 | Ключевые процедуры и state sync покрыты сценарными тестами |
| Phase 3 | AMF Core + QA/Interop Team | 2026-Q4 | Негативные и interop сценарии, telemetry/perf baselines зафиксированы |

N1 (UE / NAS 5G):

- [x] Phase 0 (Now): Socket transport + diagnostics/telemetry hooks (owner: AMF Core, ETA: done)
- [x] Phase 1: NAS PDU codec (минимальный набор Registration/Security) вместо string payload (owner: AMF Core + Protocol Team, ETA: done)
- [x] Phase 2: Процедуры UE context lifecycle + security mode handling (owner: AMF Core + Mobility/Session Team, ETA: done)
- [x] Phase 3: Negative-path тесты и interop профили (replay/tamper/timer-expiry) (owner: AMF Core + QA/Interop Team, ETA: done)

N2 (gNB / NG-RAN / NGAP):

- [x] Phase 0 (Now): NGAP-like `InitialUEMessage` framing + diagnostics/telemetry (owner: AMF Core, ETA: done)
- [x] Phase 1: ASN.1/NGAP codec и структурированные IE (owner: AMF Core + Protocol Team, ETA: done-as-text-codec)
- [x] Phase 2: Покрытие Initial Context Setup, UE Context Release, Paging (owner: AMF Core + Mobility/Session Team, ETA: done)
- [x] Phase 3: Interop/robustness тесты (malformed IE, duplicate initial UE, no-context release) (owner: AMF Core + QA/Interop Team, ETA: done)

N3 (UPF / GTP-U):

- [x] Phase 0 (Now): Socket transport payload + diagnostics/telemetry hooks (owner: AMF Core, ETA: done)
- [x] Phase 1: GTP-U-like header/TEID/encapsulation вместо plain text payload (owner: AMF Core + Protocol Team, ETA: done-as-text-codec)
- [x] Phase 2: Tunnel lifecycle (`TunnelEstablish`/`TunnelRelease`) и per-UE consistency checks (owner: AMF Core + Mobility/Session Team, ETA: done)
- [x] Phase 3: Interop/negative tests (`duplicate-tunnel-establish`, `no-tunnel-context`, `teid-mismatch`) (owner: AMF Core + QA/Interop Team, ETA: done)

N8 (UDM / SBA):

- [x] Phase 0 (Now): Socket transport payload + diagnostics/telemetry hooks (owner: AMF Core, ETA: done)
- [x] Phase 1: N8SBI-like API контракт для subscription-data (owner: AMF Core + Protocol Team, ETA: done-as-text-contract)
- [x] Phase 2: Procedural validation + per-UE request context (owner: AMF Core + Mobility/Session Team, ETA: done)
- [x] Phase 3: Interop/negative тесты (missing dataset, unknown procedure) (owner: AMF Core + QA/Interop Team, ETA: done)

N11 (SMF / SBA):

- [x] Phase 0 (Now): Socket transport payload + diagnostics/telemetry hooks (owner: AMF Core, ETA: done)
- [x] Phase 1: N11SBI-like API для create/modify/release PDU session (owner: AMF Core + Protocol Team, ETA: done-as-text-contract)
- [x] Phase 2: Процедурный state machine синхронизации PDU session context (owner: AMF Core + Mobility/Session Team, ETA: done)
- [x] Phase 3: Interop/negative tests (no-session-context, session-id-mismatch, duplicate-create) (owner: AMF Core + QA/Interop Team, ETA: done)

N12 (AUSF / SBA):

- [x] Phase 0 (Now): Socket transport payload + diagnostics/telemetry hooks (owner: AMF Core, ETA: done)
- [x] Phase 1: N12SBI-like API для auth challenge/response (owner: AMF Core + Protocol Team, ETA: done-as-text-contract)
- [x] Phase 2: Привязка к per-UE auth context и lifecycle (owner: AMF Core + Mobility/Session Team, ETA: done)
- [x] Phase 3: Негативные auth сценарии (`no-auth-context`, `auth-failed`) (owner: AMF Core + QA/Interop Team, ETA: done)

N14 (AMF-AMF / SBA):

- [x] Phase 0 (Now): Socket transport payload + diagnostics/telemetry hooks (owner: AMF Core, ETA: done)
- [x] Phase 1: HTTP/SBA контракт context transfer (owner: AMF Core + Protocol Team, ETA: done-as-text-contract)
- [x] Phase 2: Handover state transfer consistency checks (owner: AMF Core + Mobility/Session Team, ETA: done)
- [x] Phase 3: Меж-AMF interop и rollback на partial failure (owner: AMF Core + QA/Interop Team, ETA: done)

N15 (PCF / SBA):

- [x] Phase 0 (Now): Socket transport payload + diagnostics/telemetry hooks (owner: AMF Core, ETA: done)
- [x] Phase 1: N15SBI-like policy query/apply контракты (owner: AMF Core + Protocol Team, ETA: done-as-text-contract)
- [x] Phase 2: Policy association lifecycle и обновления (owner: AMF Core + Mobility/Session Team, ETA: done)
- [x] Phase 3: Negative tests и association mismatch/no-context handling (owner: AMF Core + QA/Interop Team, ETA: done)

N22 (NSSF / SBA):

- [x] Phase 0 (Now): Socket transport payload + diagnostics/telemetry hooks (owner: AMF Core, ETA: done)
- [x] Phase 1: HTTP/SBA slice selection request/response модель (owner: AMF Core + Protocol Team, ETA: done-as-text-contract)
- [x] Phase 2: Selection rules, fallback и consistency with UE context (owner: AMF Core + Mobility/Session Team, ETA: done)
- [x] Phase 3: Edge-case coverage и interop с NSSF mock (owner: AMF Core + QA/Interop Team, ETA: done)

N26 (MME / GTPv2-C):

- [x] Phase 0 (Now): Socket transport payload + diagnostics/telemetry hooks (owner: AMF Core, ETA: done)
- [x] Phase 1: GTPv2-C-like message foundation для interworking (owner: AMF Core + Protocol Team, ETA: done-as-text-codec)
- [x] Phase 2: Процедуры handover/ISR/context lifecycle и state coordination (owner: AMF Core + Mobility/Session Team, ETA: done)
- [x] Phase 3: Interop/negative tests (duplicate handover, no-context release, missing IE) (owner: AMF Core + QA/Interop Team, ETA: done)

### Roadmap/Test-plan Sync

Текущие и планируемые тесты синхронизированы с фазами roadmap:

| Interface | Phase 0 (Now, уже покрыто) | Phase 1 (план) | Phase 2 (план) | Phase 3 (план) |
| --- | --- | --- | --- | --- |
| N1 | `tests/amf_tests.cpp` (`test_amf_node_integration`, `test_n1_nas_security_fsm`) | Реализовано в `tests/amf_tests.cpp` (`test_n1_nas_security_fsm`) | Реализовано в `tests/amf_tests.cpp` (`test_n1_nas_security_fsm`) | Реализовано в `tests/n1_interop_tests.cpp` (`test_n1_replay_rejected`, `test_n1_tamper_rejected`, `test_n1_timer_expiry_rejected`) |
| N2 | `tests/amf_tests.cpp` (`test_amf_node_integration`), `tests/cli_tests.cpp` (`test_show_amf_interfaces_detail_diagnostics`) | Реализовано в `tests/n2_interop_tests.cpp` (`test_n2_initial_ue_and_context_setup`) | Реализовано в `tests/n2_interop_tests.cpp` (`test_n2_release_and_paging`) | Реализовано в `tests/n2_interop_tests.cpp` (`test_n2_missing_ie_rejected`, `test_n2_duplicate_initial_ue_rejected`, `test_n2_release_without_context_rejected`) |
| N3 | `tests/amf_tests.cpp` (`test_amf_node_integration`) | Реализовано в `tests/n3_interop_tests.cpp` (`test_n3_legacy_payload_interop`) | Реализовано в `tests/n3_interop_tests.cpp` (`test_n3_structured_lifecycle`) | Реализовано в `tests/n3_interop_tests.cpp` (`test_n3_duplicate_establish_rejected`, `test_n3_no_context_tpdu_rejected`, `test_n3_teid_mismatch_rejected`) |
| N8 | `tests/amf_tests.cpp` (`test_amf_node_integration`) | Реализовано в `tests/n8_n11_interop_tests.cpp` (`test_n8_legacy_and_structured_requests`) | Реализовано в `tests/n8_n11_interop_tests.cpp` (`test_n8_legacy_and_structured_requests`) | Реализовано в `tests/n8_n11_interop_tests.cpp` (`test_n8_missing_dataset_rejected`) |
| N11 | `tests/amf_tests.cpp` (`test_amf_node_integration`) | Реализовано в `tests/n8_n11_interop_tests.cpp` (`test_n11_create_modify_release_flow`) | Реализовано в `tests/n8_n11_interop_tests.cpp` (`test_n11_create_modify_release_flow`) | Реализовано в `tests/n8_n11_interop_tests.cpp` (`test_n11_no_context_and_mismatch_rejected`) |
| N12 | `tests/amf_tests.cpp` (`test_amf_node_integration`) | Реализовано в `tests/n12_n15_interop_tests.cpp` (`test_n12_auth_request_and_response`) | Реализовано в `tests/n12_n15_interop_tests.cpp` (`test_n12_auth_request_and_response`) | Реализовано в `tests/n12_n15_interop_tests.cpp` (`test_n12_missing_and_failed_auth_rejected`, `test_n12_pending_auth_rejected_but_context_preserved`) |
| N14 | `tests/amf_tests.cpp` (`test_amf_node_integration`) | Реализовано в `tests/n14_n22_interop_tests.cpp` (`test_n14_legacy_and_structured_transfer`) | Реализовано в `tests/n14_n22_interop_tests.cpp` (`test_n14_legacy_and_structured_transfer`) | Реализовано в `tests/n14_n22_interop_tests.cpp` (`test_n14_errors_and_rollback`) |
| N15 | `tests/amf_tests.cpp` (`test_amf_node_integration`) | Реализовано в `tests/n12_n15_interop_tests.cpp` (`test_n15_policy_query_and_update`) | Реализовано в `tests/n12_n15_interop_tests.cpp` (`test_n15_policy_query_and_update`) | Реализовано в `tests/n12_n15_interop_tests.cpp` (`test_n15_no_context_and_assoc_mismatch_rejected`, `test_n15_schema_rejects_but_context_preserved`) |
| N22 | `tests/amf_tests.cpp` (`test_amf_node_integration`) | Реализовано в `tests/n14_n22_interop_tests.cpp` (`test_n22_selection_and_fallback_flow`) | Реализовано в `tests/n14_n22_interop_tests.cpp` (`test_n22_selection_and_fallback_flow`) | Реализовано в `tests/n14_n22_interop_tests.cpp` (`test_n22_invalid_and_context_errors`) |
| N26 | `tests/amf_tests.cpp` (`test_amf_node_integration`) | Реализовано в `tests/n26_interop_tests.cpp` (`test_n26_handover_and_context_transfer`) | Реализовано в `tests/n26_interop_tests.cpp` (`test_n26_isr_activate_deactivate_flow`) | Реализовано в `tests/n26_interop_tests.cpp` (`test_n26_missing_mandatory_ie_rejected`, `test_n26_duplicate_handover_rejected`, `test_n26_no_context_release_rejected`) |
| SBI (reference stack) | `tests/amf_tests.cpp` (`test_network_sbi_response_handling`), `tests/cli_tests.cpp` (`test_show_amf_interfaces_errors_last`, `test_show_amf_telemetry`), `tests/config_logging_tests.cpp` (`test_yaml_config_load`, `test_json_config_load`) | Поддерживать как baseline для SBA-интерфейсов | Расширять с процедурными сценариями SBA | Использовать для interop/perf baselines |

### Ограничения прототипа

- Для N1/N2/N3/N8/N11/N12/N14/N15/N22/N26 доступен transport socket-level (tcp/udp). Для N1 реализован NAS5G codec + registration/security FSM (uplink `simulate n1` принимает `registration-request`, `authentication-response`, `security-mode-complete`, `deregistration-request` и `NAS5G|...` сообщения), для N2 реализован structured NGAP-like codec с процедурами `InitialUEMessage`, `InitialContextSetupRequest`, `UEContextReleaseCommand`, `Paging`, IE validation и `ErrorIndication` на malformed/no-context сценариях, для N3 реализован structured GTPU-like codec (`simulate n3` принимает legacy aliases и `GTPU|message=...`) с per-UE tunnel context (`TEID`, `QFI`, seq counters), lifecycle validation, ErrorIndication и binary-header field emulation (`version/flags/type/length/seq`), для N8 реализован structured N8SBI-like контракт (`simulate n8` принимает default `get-am-data`, legacy aliases и `N8SBI|procedure=...`) с per-UE subscription context и ErrorIndication на malformed сценариях, для N11 реализован structured N11SBI-like контракт (`simulate n11` принимает `create|modify|release` и `N11SBI|procedure=...`) с per-UE PDU session context/state validation и ErrorIndication, для N12 реализован structured N12SBI-like auth flow (`simulate n12` принимает default `auth-request`, aliases и `N12SBI|procedure=...`) с per-UE auth context (`RAND`, `AUTN`, `XRES*`) и stricter guardrails на overlapping/unsupported auth flows, для N14 реализован structured N14SBI-like context transfer (`simulate n14` принимает target-AMF shortcut, lifecycle aliases и `N14SBI|procedure=...`) с per-UE transfer context (`target AMF`, `transfer-id`, `version`, `state`) и rollback/ErrorIndication, для N15 реализован structured N15SBI-like policy flow (`simulate n15` принимает default `get-am-policy`, aliases и `N15SBI|procedure=...`) с per-UE policy association context, schema-style validation (`policy-type`, `SNSSAI`) и ErrorIndication, для N22 реализован structured N22SBI-like slice selection (`simulate n22` принимает SNSSAI shortcut, lifecycle aliases и `N22SBI|procedure=...`) с per-UE selection context (`selection-id`, `selected SNSSAI`, `allowed list`, `state`), fallback selection rules и ErrorIndication, для N26 реализован structured GTPV2C-like codec (`simulate n26` принимает legacy aliases и `GTPV2C|procedure=...`) с per-UE MME context (`MME-TEID`, `ENB-TEID`, sequence, state), procedural validation и `ErrorIndication`, для SBI реализован HTTP/JSON request+response handling (успех операции только при HTTP 2xx). Для остальных интерфейсов прикладной 3GPP слой пока остается упрощенным payload-форматом.
- Модель multi-user симулируется в одном процессе CLI через смену owner-id команды.
- Конфигурация хранится в памяти процесса, без персистентного хранилища.

## 2. Структура проекта

- `CMakeLists.txt` - конфигурация сборки.
- `include/amf/interfaces.hpp` - интерфейсы и модели AMF.
- `include/amf/amf.hpp` - класс `AmfNode`.
- `include/amf/cli.hpp` - интерфейс CLI shell.
- `include/amf/app/amf_services.hpp` - application service слой (use-case API).
- `include/amf/adapters/console_adapters.hpp` - консольные адаптеры N1/N2/N3/N8/N11/N12/N14/N15/N22/N26/SBI.
- `include/amf/adapters/network_adapters.hpp` - socket-адаптеры N1/N2/N3/N8/N11/N12/N14/N15/N22/N26/SBI.
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
- `src/adapters/network_adapters.cpp` - реализация socket-адаптеров и SBI HTTP request/response handling.
- `src/config/runtime_config.cpp` - парсер runtime-конфига (.json/.yaml/.yml).
- `src/logging/file_logger.cpp` - реализация файлового логгера.
- `src/modules/registration.cpp` - реализация Registration.
- `src/modules/mobility.cpp` - реализация Mobility.
- `src/modules/control_plane.cpp` - реализация Control Plane.
- `src/modules/user_plane.cpp` - реализация User Plane.
- `src/modules/interworking.cpp` - реализация Interworking.
- `src/modules/session_management.cpp` - legacy-реализация Session Management.
- `src/main.cpp` - точка входа и выбор mock/network адаптеров по runtime-конфигу.
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

Для реального transport-режима переключите `network_adapters.mode` в `network` в runtime-config.

Запуск тестов:

```powershell
ctest --test-dir build --output-on-failure -C Debug
```

В этом наборе тестов теперь есть:

- `amf_tests`
- `amf_cli_tests`
- `amf_config_logging_tests`
- `n1_interop_tests`
- `n2_interop_tests`
- `n3_interop_tests`
- `n26_interop_tests`
- `n8_n11_interop_tests`
- `n12_n15_interop_tests`
- `n14_n22_interop_tests`

Дополнительно в `amf_tests` есть интеграционный socket-тест SBI response handling:

- HTTP `200` => `notify_sbi` возвращает success, растет `success_count` интерфейса SBI
- HTTP `500` => `notify_sbi` возвращает fail, растет `error_count`, `status_reason=service-reject`
- `no response / timeout` => `notify_sbi` возвращает fail, растет `sbi_timeout_failures`
- `malformed HTTP` => `notify_sbi` возвращает fail, растет `sbi_non_2xx_failures`

## 4. SBI Network Semantics

В режиме `network_adapters.mode=network` для SBI используется HTTP/1.1 over TCP.

- Формируется POST ` /namf/<service> ` c JSON-body (`service`, `payload`, `timestamp`).
- Адаптер читает HTTP status line из ответа.
- HTTP parser поддерживает длинные заголовки и `Transfer-Encoding: chunked`.
- Валидация status line строгая: ожидается формат `HTTP/<digit>.<digit> <3-digit-code> [reason]`.
- Успех операции считается только при коде `2xx`.
- Любой non-`2xx`, ошибка сокета/подключения или невалидный HTTP-ответ трактуется как fail и пробрасывается в диагностику интерфейса SBI (`service-reject`).
- Поддержаны resilience-механики:
  - timeout (`network_adapters.sbi_timeout_ms`)
  - retry (`network_adapters.sbi_retry_count`)
  - circuit-breaker (`network_adapters.sbi_cb_failure_threshold`, `network_adapters.sbi_cb_reset_seconds`)
- В `show amf interfaces detail` для SBI выводятся отдельные счетчики причин отказа:
  - `fail-timeout`
  - `fail-connect`
  - `fail-non-2xx`
  - `circuit-open-reject`
  - `circuit-open`

## 5. Быстрый старт (конкуренция owner-id)

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

## 6. Команды CLI

### Exec mode (AMF#)

- `show session`
- `show policy`
- `session owner <owner-id>`
- `session role operator|admin`
- `policy reload`
- `show amf status`
- `show amf stats`
- `show amf telemetry [window-seconds]`
- `show amf interfaces errors last [N] [iface <name>] [reason <value>]`
- `show amf ue [imsi]`
- `show running config`
- `show configuration candidate`
- `show configuration diff`
- `show configuration lock`
- `runtime-config reload [path]` (алиасы: `runtime_config`, `runtimeconfig`)
- `amf start|stop|degrade|recover|tick`
- `ue register <imsi> <tai>`
- `ue deregister <imsi>`
- `simulate n2 <imsi> <ngap-msg>` (например: `registration-request`, `initial-context-setup`, `ue-context-release`, `paging`, либо `NGAP|procedure=...|...`)
- `simulate sbi <service> <payload>`
- `simulate n1 <imsi> <nas-msg>` (например: `registration-request`, `authentication-response`, `security-mode-complete`, `deregistration-request`, либо `NAS5G|dir=UL|message=...`)
- `simulate n3 <imsi> <gtpu-msg>` (например: `tunnel-establish`, `uplink-data`, `downlink-data`, `tunnel-release`, либо `GTPU|message=...|...`)
- `simulate n8 <imsi> [n8-msg]` (например: `get-am-data`, `get-smf-selection-data`, `get-ue-context-in-smf-data`, либо `N8SBI|procedure=...|dataset=...`)
- `simulate n11 <imsi> <n11-op>` (например: `create`, `modify`, `release`, либо `N11SBI|procedure=...|...`)
- `simulate n12 <imsi> [n12-msg]` (например: `auth-request`, `auth-response`, либо `N12SBI|procedure=...|...`)
- `simulate n14 <imsi> [n14-msg]`
- `simulate n15 <imsi> [n15-msg]` (например: `get-am-policy`, `get-sm-policy`, `update-policy-association`, либо `N15SBI|procedure=...|...`)
- `simulate n22 <imsi> [n22-msg]`
- `simulate n26 <imsi> <gtpv2c-op>` (например: `handover`, `context-transfer`, `isr-activate`, `isr-deactivate`, `release`, либо `GTPV2C|procedure=...|...`)
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

## 6.1. Transport Telemetry

Команда `show amf telemetry [window-seconds]` выводит transport-метрики по интерфейсам за скользящее окно времени (по умолчанию 60 секунд):

- `attempts`: количество попыток операций в окне
- `successes`: количество успешных операций
- `success-rate`: доля успешных операций в процентах
- `p50`: медианная latency в миллисекундах
- `p95`: 95-й перцентиль latency в миллисекундах

## 7. Network Adapter Runtime Config

Секция `network_adapters` позволяет переключать интерфейсы между mock и реальным сокетным transport-стеком без перекомпиляции.

Ключи:

- `mode`: `mock` или `network`
- `sbi_timeout_ms`: timeout чтения ответа SBI (мс)
- `sbi_retry_count`: число retry после первой попытки
- `sbi_cb_failure_threshold`: число подряд неуспехов до открытия circuit-breaker
- `sbi_cb_reset_seconds`: время открытия circuit-breaker перед новой попыткой
- `<iface>_address`: IPv4/hostname endpoint
- `<iface>_port`: порт endpoint
- `<iface>_transport`: `tcp` или `udp`

Поддерживаемые `<iface>`:

- `n1`, `n2`, `n3`, `n8`, `n11`, `n12`, `n14`, `n15`, `n22`, `n26`, `sbi`

Пример:

```yaml
network_adapters:
  mode: "network"
  sbi_timeout_ms: 2000
  sbi_retry_count: 2
  sbi_cb_failure_threshold: 3
  sbi_cb_reset_seconds: 10
  n2_address: "10.10.10.20"
  n2_port: 38412
  n2_transport: "udp"
  sbi_address: "10.10.10.30"
  sbi_port: 7777
  sbi_transport: "tcp"
```

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

## 8. Lock and TTL semantics

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

## 9. Commit confirmed semantics

1. Выполняется `commit confirmed <seconds>`.
2. Изменение применяется сразу в running.
3. Если до истечения таймера не вызвать `confirm`, выполняется auto-rollback.

Пример:

```text
AMF(config)# commit confirmed 30
AMF(config)# confirm
```

## 10. Валидации

- `owner-id`: `[A-Za-z0-9_-]`, длина до 32.
- `role`: `operator` или `admin`.
- `mcc`: ровно 3 цифры.
- `mnc`: ровно 2 цифры.
- `local-address` / `bind-address`: IPv4.
- `port`: 1..65535.
- `nf-instance`: `[A-Za-z0-9_-]`, длина до 32.

## 11. Audit logging

CLI пишет аудит-события в файл `amf-audit.log` (в текущем рабочем каталоге процесса). Логируются:

- `candidate-lock`, `candidate-renew`, `candidate-unlock`, `candidate-force-unlock`
- `commit`, `commit-confirmed`, `confirm-commit`, `rollback`, `auto-rollback`
- `policy-reload`

Формат строки:

```text
<UTC timestamp> owner=<owner-id> role=<operator|admin> action=<action> result=<success|deny> detail="..."
```

## 12. Runtime config (YAML/JSON)

Поддерживаются форматы `.yaml`, `.yml`, `.json`.

Поддерживаемые поля:

- `mcc`, `mnc`
- `log_file`, `audit_log_file`, `rbac_policy_file`
- `n2.local_address`, `n2.port`
- `sbi.bind_address`, `sbi.port`, `sbi.nf_instance`
- `alarm_thresholds.warning_error_rate_percent`, `alarm_thresholds.critical_error_rate_percent`, `alarm_thresholds.critical_error_count`, `alarm_thresholds.admin_down_warning`
- `network_adapters.mode`
- `network_adapters.sbi_timeout_ms`, `network_adapters.sbi_retry_count`
- `network_adapters.sbi_cb_failure_threshold`, `network_adapters.sbi_cb_reset_seconds`
- `network_adapters.<iface>_address`, `network_adapters.<iface>_port`, `network_adapters.<iface>_transport`

Где `<iface>`: `n1`, `n2`, `n3`, `n8`, `n11`, `n12`, `n14`, `n15`, `n22`, `n26`, `sbi`.

Примеры готовы в файлах:

- `config/amf-config.yaml`
- `config/amf-config.json`

## 13. File logging

Помимо `amf-audit.log`, добавлен общий процессный лог в файл `log_file` из runtime-конфига (по умолчанию `amf.log`).
В этот лог пишутся:

- старт/останов процесса
- применение runtime-конфига
- N2/SBI события из консольных адаптеров
