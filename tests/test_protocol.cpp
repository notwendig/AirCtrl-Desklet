/**
 * @file test_protocol.cpp
 * @brief Standalone regression tests for the Philips encrypted-CoAP layer.
 */
#include "udp_device.hpp"

#include <aioairctrl/client.hpp>
#include <aioairctrl/encryption.hpp>

#include <chrono>
#include <exception>
#include <iostream>
#include <stdexcept>
#include <string>

namespace {
using namespace std::chrono_literals;

void require(bool condition, const char* message) {
    if (!condition) throw std::runtime_error(message);
}

aioairctrl::ClientOptions optionsFor(const UdpDevice& device) {
    aioairctrl::ClientOptions options;
    options.port = device.port;
    options.timeout = 2s;
    options.control_timeout = 2s;
    options.observe_idle_timeout = 2s;
    return options;
}

void realAc2729CiphertextDecrypts() {
    // Public AC2729/10 capture published with aioairctrl 0.3.1. Only benign
    // protocol/model fields are asserted; device identifiers are not copied
    // into this repository as plaintext.
    static constexpr char encrypted[] =
        "86ABD25E53DA53C9634039EEF8A4C550BBD2C26ECBAEDC11E13FC94E7AC7B700986CF45"
        "6A8354BA8401F2E1895ED0FE2FCCC7BEEFF598116A4B59C0C790D75421E78CA15D69902"
        "BF427F58C62F984E2753D3B1F15394B2284B72AC46C9B892D488BC0D1EC83277A896C85"
        "514E561BBF87E9D4443BA7E9179C23D8EE857682508B1C30D2A6C7147CD06748EF4A4A7"
        "B2B7F769E72DAEAEB0C1943902474ED127CA3BEDACCABF5B7000123007DCAB4F23D09BA"
        "029132D377362FA3F78B70118AD818CD892EA921665B9FF19E749B3BF15E907EA3BCE1E"
        "B5B9130FD57D539F488661B08E19EC41532DE14F2A31C147BBEB068F1106955922BDC70"
        "089331C56DC981010B6899D3E5DA2B2B827D3BAFC65110DA512CAD6EC2B43B181872E14"
        "2B7B711C8BE4B8E8EA7A8557586958CDD29076111CF74AEC0B8F3CF971964B3222E7582"
        "86127BAED34CE9D7D8691FF119287088B18539C7F3D35BB5AD7965B53A72B6638EB4732"
        "4C1EFFC78827A3BF5DE5ECDB03DD338513A626E591CF1B07870EF6220CAD83452133304"
        "034E96416A7625AE91DA9CEAD3C4B3FA2CD3274D709427709E8185FC102B7E50264699A"
        "AC0032806D7E0F7861EC7F3CCA3F6F71CA019607A3A7D05722BB951401CE3985EE71846"
        "A98EB643428E81C2CE8B7EB0DA6E89E2FB311DC0CF3FF2961DA5C124B6E88B146A344A0"
        "560A200224863E2EC90D913C239B006B84AB77A5B7DD471E4F8D696400C1C598F990621"
        "2DE55C60E59F1493994CFD87DFF561798C53A9D5ED7A0B5FC690B90A9ED61F02928AD7C"
        "20C324FCDBB9210476F295558DC593CDB59354346BA13CF5877DDE05864E04A17E0CD64"
        "F73BB66B8D0444C0B8666ABF21F0FCDEF8A526DA68F72955F4D7AA9B2F33DF075D09178"
        "C1D9EA0FF390E4CB0E33DC86D4714AE3FED5548D7E8040A894A1956A8D73387242DA3A0"
        "589C24941C61186D7B95F3D3C8651962DA005306472F919CFB0C9182E3ED71985BBE406"
        "9D9B9C7FE37917202A32568EE6B4";

    aioairctrl::EncryptionContext encryption;
    const aioairctrl::Json status = aioairctrl::Json::parse(encryption.decrypt(encrypted));
    const aioairctrl::Json& reported = status.at("state").at("reported");
    require(reported.at("type") == "AC2729", "real vector: wrong device type");
    require(reported.at("modelid") == "AC2729/10", "real vector: wrong model");
    require(reported.at("pwr") == "0", "real vector: wrong power value");
    require(reported.at("func") == "P", "real vector: wrong function value");
    require(reported.at("rh") == 52, "real vector: wrong humidity");

    std::string tampered = encrypted;
    tampered[20] = tampered[20] == '0' ? '1' : '0';
    bool rejected = false;
    try { static_cast<void>(encryption.decrypt(tampered)); }
    catch (const aioairctrl::DigestMismatchException&) { rejected = true; }
    require(rejected, "tampered real vector was accepted");
}

void wrongAckMidIsIgnored() {
    UdpDevice device({false, true, false});
    aioairctrl::Client client("127.0.0.1", optionsFor(device));
    const aioairctrl::Json status = client.get_status();
    require(status.at("pwr") == "1", "wrong ACK MID replaced the valid sync response");
    client.shutdown();
    require(!device.failed.load(), "UDP simulator failed during wrong-ACK test");
}

void invalidReportedShapeIsRejected() {
    UdpDevice device({true, false, false});
    aioairctrl::Client client("127.0.0.1", optionsFor(device));
    bool rejected = false;
    try { static_cast<void>(client.get_status()); }
    catch (const std::runtime_error& error) {
        rejected = std::string(error.what()).find("state.reported") != std::string::npos;
    }
    client.shutdown();
    require(rejected, "invalid state.reported was accepted");
    require(!device.failed.load(), "UDP simulator failed during invalid-status test");
}

void rejectedControlResynchronizesOnlyOnce() {
    UdpDevice device({false, false, true});
    aioairctrl::Client client("127.0.0.1", optionsFor(device));
    const bool accepted = client.set_control_values({{"pwr", "0"}}, 2, true);
    client.shutdown();
    require(!accepted, "rejected control unexpectedly succeeded");
    require(device.controls.load() == 3, "wrong control attempt count");
    require(device.syncs.load() == 2, "control rejection did not perform exactly one resync");
    require(!device.failed.load(), "UDP simulator failed during resync test");
}
} // namespace

int main() {
    try {
        realAc2729CiphertextDecrypts();
        wrongAckMidIsIgnored();
        invalidReportedShapeIsRejected();
        rejectedControlResynchronizesOnlyOnce();
        std::cout << "Protocol tests passed\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "Protocol test failed: " << error.what() << '\n';
        return 1;
    }
}
