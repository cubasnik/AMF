#include <iostream>
#include <string>

#include "amf/amf.hpp"

namespace {

class TestN2 final : public amf::IN2Interface {
public:
    void deliver_nas(const std::string&, const std::string&) override {}
};

class TestN14 final : public amf::IN14Interface {
public:
    void transfer_amf_context(const std::string& imsi, const std::string& request) override {
        ++transfers;
        last_imsi = imsi;
        last_request = request;
    }

    int transfers {0};
    std::string last_imsi;
    std::string last_request;
};

class TestN22 final : public amf::IN22Interface {
public:
    void select_network_slice(const std::string& imsi, const std::string& request) override {
        ++selections;
        last_imsi = imsi;
        last_request = request;
    }

    int selections {0};
    std::string last_imsi;
    std::string last_request;
};

class TestSbi final : public amf::ISbiInterface {
public:
    bool notify_service(const std::string&, const std::string&) override { return true; }
};

bool check(bool condition, const std::string& message) {
    if (!condition) {
        std::cerr << "[FAIL] " << message << "\n";
        return false;
    }
    return true;
}

bool contains(const std::string& text, const std::string& needle) {
    return text.find(needle) != std::string::npos;
}

std::string find_field_line_value(const std::string& text, const std::string& key) {
    const std::size_t pos = text.find(key);
    if (pos == std::string::npos) {
        return {};
    }
    const std::size_t value_pos = pos + key.size();
    const std::size_t line_end = text.find('\n', value_pos);
    return text.substr(value_pos, line_end == std::string::npos ? std::string::npos : line_end - value_pos);
}

amf::AmfNode make_node(TestN2& n2, TestSbi& sbi, TestN14& n14, TestN22& n22) {
    amf::AmfPeerInterfaces peers {};
    peers.n14 = &n14;
    peers.n22 = &n22;
    return amf::AmfNode(n2, sbi, peers);
}

bool test_n14_legacy_and_structured_transfer() {
    TestN2 n2;
    TestSbi sbi;
    TestN14 n14;
    TestN22 n22;
    auto node = make_node(n2, sbi, n14, n22);

    const std::string imsi = "250030000014001";
    bool ok = true;

    ok &= check(node.start(), "AMF should start");
    ok &= check(node.register_ue(imsi, "250-03"), "UE should be registered");
    ok &= check(node.transfer_n14_context(imsi, "amf-b"), "Legacy N14 transfer should be accepted");
    ok &= check(contains(n14.last_request, "N14SBI/1\n"), "N14 should emit structured payload");
    ok &= check(contains(n14.last_request, "procedure=ContextTransfer"), "Legacy transfer should map to ContextTransfer");
    ok &= check(contains(n14.last_request, "ie.target-amf=amf-b"), "Legacy transfer should preserve target AMF");
    ok &= check(contains(n14.last_request, "response.model=N14TransferResponse"), "N14 should emit response model");
    ok &= check(contains(n14.last_request, "response.status=success"), "N14 success should emit success status");

    const std::string structured_imsi = "250030000014002";
    ok &= check(node.register_ue(structured_imsi, "250-03"), "Structured UE should be registered");
    ok &= check(node.transfer_n14_context(structured_imsi, "N14SBI|procedure=PrepareHandover|target-amf=amf-c|source-amf=amf-a|transfer-id=ctx-42|ue-context-version=7"), "PrepareHandover should be accepted");
    ok &= check(contains(n14.last_request, "procedure=PrepareHandover"), "N14 should emit PrepareHandover");
    ok &= check(contains(n14.last_request, "response.procedure=PrepareHandover"), "Response should expose PrepareHandover procedure");
    ok &= check(node.transfer_n14_context(structured_imsi, "N14SBI|procedure=ContextTransfer|target-amf=amf-c|source-amf=amf-a|transfer-id=ctx-42|ue-context-version=7"), "ContextTransfer after prepare should be accepted");
    ok &= check(contains(n14.last_request, "ie.result=context-transferred"), "ContextTransfer should report transferred result");
    ok &= check(contains(n14.last_request, "response.transfer-state=context-transferred"), "Response should expose transferred state");
    ok &= check(node.transfer_n14_context(structured_imsi, "N14SBI|procedure=CompleteTransfer|target-amf=amf-c|transfer-id=ctx-42"), "CompleteTransfer should be accepted");
    ok &= check(contains(n14.last_request, "procedure=CompleteTransfer"), "N14 should emit CompleteTransfer");
    ok &= check(contains(n14.last_request, "ie.result=completed"), "CompleteTransfer should report completed result");
    ok &= check(contains(n14.last_request, "response.transfer-id=ctx-42"), "Response should expose transfer id");
    ok &= check(contains(n14.last_request, "response.correlation-id=ctx-42:"), "Response should expose correlation id");

    return ok;
}

bool test_n14_errors_and_rollback() {
    TestN2 n2;
    TestSbi sbi;
    TestN14 n14;
    TestN22 n22;
    auto node = make_node(n2, sbi, n14, n22);

    const std::string imsi = "250030000014003";
    bool ok = true;

    ok &= check(node.start(), "AMF should start");
    ok &= check(node.register_ue(imsi, "250-03"), "UE should be registered");
    ok &= check(!node.transfer_n14_context(imsi, "N14SBI|procedure=ContextTransfer|target-amf=amf-z|source-amf=amf-a|transfer-id=ctx-9|ue-context-version=1"), "Structured ContextTransfer without prepare should be rejected");
    ok &= check(contains(n14.last_request, "ie.cause=no-prepare-context"), "Missing prepare should emit no-prepare-context");
    ok &= check(contains(n14.last_request, "response.status=error"), "N14 rejection should emit error status");

    ok &= check(node.transfer_n14_context(imsi, "N14SBI|procedure=PrepareHandover|target-amf=amf-z|source-amf=amf-a|transfer-id=ctx-9|ue-context-version=1"), "Prepare should succeed");
    ok &= check(!node.transfer_n14_context(imsi, "N14SBI|procedure=ContextTransfer|target-amf=amf-z|source-amf=amf-a|transfer-id=ctx-bad|ue-context-version=1"), "Transfer-id mismatch should be rejected");
    ok &= check(contains(n14.last_request, "ie.cause=transfer-id-mismatch"), "Mismatch should emit transfer-id-mismatch");
    ok &= check(contains(n14.last_request, "response.cause=transfer-id-mismatch"), "Response should mirror rejection cause");
    ok &= check(node.transfer_n14_context(imsi, "rollback-context"), "Rollback should be accepted while transfer is pending");
    ok &= check(contains(n14.last_request, "procedure=RollbackContext"), "Rollback should emit rollback procedure");
    ok &= check(contains(n14.last_request, "ie.result=rolled-back"), "Rollback should report rolled-back result");
    ok &= check(contains(n14.last_request, "response.transfer-state=rolled-back"), "Rollback response should expose rolled-back state");
    ok &= check(node.transfer_n14_context(imsi, "N14SBI|procedure=PrepareHandover|target-amf=amf-z|source-amf=amf-a|transfer-id=ctx-10|ue-context-version=1"), "Prepare should succeed again after rollback");

    return ok;
}

bool test_n14_missing_ie_and_unsupported_procedure_rejected() {
    TestN2 n2;
    TestSbi sbi;
    TestN14 n14;
    TestN22 n22;
    auto node = make_node(n2, sbi, n14, n22);

    const std::string imsi = "250030000014004";
    bool ok = true;

    ok &= check(node.start(), "AMF should start");
    ok &= check(node.register_ue(imsi, "250-03"), "UE should be registered");
    ok &= check(!node.transfer_n14_context(imsi, "N14SBI|procedure=PrepareHandover|target-amf=amf-b|source-amf=amf-a|transfer-id=ctx-11"),
        "PrepareHandover without ue-context-version should be rejected");
    ok &= check(contains(n14.last_request, "ie.cause=missing-mandatory-ie"), "Missing IE should emit missing-mandatory-ie");
    ok &= check(!node.transfer_n14_context(imsi, "N14SBI|procedure=AbortTransfer|transfer-id=ctx-11"),
        "Unsupported N14 procedure should be rejected");
    ok &= check(contains(n14.last_request, "ie.cause=unsupported-procedure"), "Unsupported N14 procedure should emit unsupported-procedure");

    return ok;
}

bool test_n14_prepare_collision_and_target_mismatch_rejected() {
    TestN2 n2;
    TestSbi sbi;
    TestN14 n14;
    TestN22 n22;
    auto node = make_node(n2, sbi, n14, n22);

    const std::string imsi = "250030000014005";
    bool ok = true;

    ok &= check(node.start(), "AMF should start");
    ok &= check(node.register_ue(imsi, "250-03"), "UE should be registered");
    ok &= check(node.transfer_n14_context(imsi, "N14SBI|procedure=PrepareHandover|target-amf=amf-b|source-amf=amf-a|transfer-id=ctx-12|ue-context-version=1"),
        "Initial prepare should be accepted");
    ok &= check(!node.transfer_n14_context(imsi, "N14SBI|procedure=PrepareHandover|target-amf=amf-b|source-amf=amf-a|transfer-id=ctx-13|ue-context-version=1"),
        "Repeated prepare while context pending should be rejected");
    ok &= check(contains(n14.last_request, "ie.cause=handover-already-prepared"), "Repeated prepare should emit handover-already-prepared");
    ok &= check(!node.transfer_n14_context(imsi, "N14SBI|procedure=ContextTransfer|target-amf=amf-c|source-amf=amf-a|transfer-id=ctx-12|ue-context-version=1"),
        "ContextTransfer with different target AMF should be rejected");
    ok &= check(contains(n14.last_request, "ie.cause=target-amf-mismatch"), "Target mismatch should emit target-amf-mismatch");

    return ok;
}

bool test_n22_selection_and_fallback_flow() {
    TestN2 n2;
    TestSbi sbi;
    TestN14 n14;
    TestN22 n22;
    auto node = make_node(n2, sbi, n14, n22);

    const std::string imsi = "250030000022001";
    bool ok = true;

    ok &= check(node.start(), "AMF should start");
    ok &= check(node.register_ue(imsi, "250-03"), "UE should be registered");
    ok &= check(node.select_n22_slice(imsi, "1-010203"), "Legacy slice selection should be accepted");
    ok &= check(contains(n22.last_request, "N22SBI/1\n"), "N22 should emit structured payload");
    ok &= check(contains(n22.last_request, "procedure=SelectSlice"), "N22 should emit SelectSlice");
    ok &= check(contains(n22.last_request, "ie.selected-snssai=1-010203"), "Selected SNSSAI should match request");
    ok &= check(contains(n22.last_request, "response.model=N22SelectionResponse"), "N22 should emit response model");
    ok &= check(contains(n22.last_request, "response.status=success"), "N22 success should emit success status");

    ok &= check(node.select_n22_slice(imsi, "N22SBI|procedure=UpdateSelection|requested-snssai=1-999999|allowed-snssai=1-010203,1-112233|fallback-snssai=1-112233"), "UpdateSelection with fallback should be accepted");
    ok &= check(contains(n22.last_request, "procedure=UpdateSelection"), "N22 should emit UpdateSelection");
    ok &= check(contains(n22.last_request, "ie.selected-snssai=1-112233"), "Fallback should select allowed slice");
    ok &= check(contains(n22.last_request, "ie.selection-result=fallback"), "Fallback path should be marked as fallback");
    ok &= check(contains(n22.last_request, "response.selection-state=fallback"), "Response should expose fallback selection state");

    const std::string selection_id = find_field_line_value(n22.last_request, "ie.selection-id=");
    ok &= check(!selection_id.empty(), "Selection flow should expose selection id");
    ok &= check(node.select_n22_slice(imsi, "N22SBI|procedure=ReleaseSelection|selection-id=" + selection_id), "ReleaseSelection should be accepted");
    ok &= check(contains(n22.last_request, "procedure=ReleaseSelection"), "Release should emit release procedure");
    ok &= check(contains(n22.last_request, "ie.selection-result=released"), "Release should report released result");
    ok &= check(contains(n22.last_request, "response.selection-id=" + selection_id), "Response should expose release selection id");
    ok &= check(contains(n22.last_request, "response.correlation-id=" + selection_id + ":"), "Response should expose selection correlation id");

    return ok;
}

bool test_n22_invalid_and_context_errors() {
    TestN2 n2;
    TestSbi sbi;
    TestN14 n14;
    TestN22 n22;
    auto node = make_node(n2, sbi, n14, n22);

    const std::string imsi = "250030000022002";
    bool ok = true;

    ok &= check(node.start(), "AMF should start");
    ok &= check(node.register_ue(imsi, "250-03"), "UE should be registered");
    ok &= check(!node.select_n22_slice(imsi, "N22SBI|procedure=SelectSlice|requested-snssai=bad|allowed-snssai=1-010203"), "Invalid SNSSAI should be rejected");
    ok &= check(contains(n22.last_request, "ie.cause=invalid-snssai"), "Invalid SNSSAI should emit invalid-snssai");
    ok &= check(contains(n22.last_request, "response.status=error"), "N22 rejection should emit error status");
    ok &= check(!node.select_n22_slice(imsi, "release-selection"), "Release without context should be rejected");
    ok &= check(contains(n22.last_request, "ie.cause=no-selection-context"), "Release without context should emit no-selection-context");
    ok &= check(contains(n22.last_request, "response.cause=no-selection-context"), "Response should mirror no-selection-context cause");

    ok &= check(node.select_n22_slice(imsi, "1-010203"), "Initial selection should succeed");
    const std::string selection_id = find_field_line_value(n22.last_request, "ie.selection-id=");
    ok &= check(!selection_id.empty(), "Initial selection should expose selection id");
    ok &= check(!node.select_n22_slice(imsi, "N22SBI|procedure=UpdateSelection|requested-snssai=1-999999|allowed-snssai=1-010203"), "Unsupported SNSSAI without fallback should be rejected");
    ok &= check(contains(n22.last_request, "ie.cause=unsupported-snssai"), "Unsupported SNSSAI should emit unsupported-snssai");
    ok &= check(node.select_n22_slice(imsi, "N22SBI|procedure=ReleaseSelection|selection-id=" + selection_id), "Previous selection should remain releasable after rejected update");

    return ok;
}

bool test_n22_fallback_and_procedure_errors() {
    TestN2 n2;
    TestSbi sbi;
    TestN14 n14;
    TestN22 n22;
    auto node = make_node(n2, sbi, n14, n22);

    const std::string imsi = "250030000022003";
    bool ok = true;

    ok &= check(node.start(), "AMF should start");
    ok &= check(node.register_ue(imsi, "250-03"), "UE should be registered");
    ok &= check(!node.select_n22_slice(imsi, "N22SBI|procedure=SelectSlice|requested-snssai=1-999999|allowed-snssai=1-010203|fallback-snssai=bad"),
        "Invalid fallback SNSSAI should be rejected");
    ok &= check(contains(n22.last_request, "ie.cause=invalid-fallback-snssai"), "Invalid fallback should emit invalid-fallback-snssai");

    ok &= check(!node.select_n22_slice(imsi, "N22SBI|procedure=SelectSlice|requested-snssai=1-999999|allowed-snssai=1-010203|fallback-snssai=1-112233"),
        "Fallback outside allowed list should be rejected");
    ok &= check(contains(n22.last_request, "ie.cause=fallback-not-allowed"), "Disallowed fallback should emit fallback-not-allowed");

    ok &= check(node.select_n22_slice(imsi, "1-010203"), "Initial selection should succeed");
    ok &= check(!node.select_n22_slice(imsi, "N22SBI|procedure=ReleaseSelection|selection-id=nssf-bad"), "Release with selection-id mismatch should be rejected");
    ok &= check(contains(n22.last_request, "ie.cause=selection-id-mismatch"), "Selection-id mismatch should emit selection-id-mismatch");

    ok &= check(!node.select_n22_slice(imsi, "N22SBI|procedure=DeleteSelection|selection-id=nssf-bad"), "Unsupported N22 procedure should be rejected");
    ok &= check(contains(n22.last_request, "ie.cause=unsupported-procedure"), "Unsupported N22 procedure should emit unsupported-procedure");

    return ok;
}

}  // namespace

int main() {
    bool ok = true;
    ok &= test_n14_legacy_and_structured_transfer();
    ok &= test_n14_errors_and_rollback();
    ok &= test_n14_missing_ie_and_unsupported_procedure_rejected();
    ok &= test_n14_prepare_collision_and_target_mismatch_rejected();
    ok &= test_n22_selection_and_fallback_flow();
    ok &= test_n22_invalid_and_context_errors();
    ok &= test_n22_fallback_and_procedure_errors();

    if (!ok) {
        return 1;
    }

    std::cout << "[PASS] N14/N22 interop and hardening tests passed.\n";
    return 0;
}