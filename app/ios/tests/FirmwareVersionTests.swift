@main
enum FirmwareVersionTests {
    static func main() {
        let cases: [(String, String, Bool)] = [
            ("0.3.0", "0.3.1", false),
            ("0.3.0", "0.4.0", false),
            ("0.3.0", "0.3.0", false),
            ("0.3.1", "0.3.0", true),
            ("0.10.0", "0.9.9", true),
            ("0.9.9", "0.10.0", false),
            ("1.0.0", "0.255.255", true),
            ("1.0.0", "2.0.0", false),
            ("0.03.0", "0.3.0", false)
        ]
        for (offered, installed, expected) in cases {
            precondition(FirmwareVersion.isNewer(offered, than: installed) == expected,
                         "offered \(offered), installed \(installed)")
        }
        for invalid in ["", "unknown", "0.3", "0.3.0.1", "0..3", "-1.0.0", "+1.0.0",
                        " 1.0.0", "1.0.0-beta", "999999999999999999999999999999.0.0"] {
            precondition(!FirmwareVersion.isNewer(invalid, than: "0.3.0"))
            precondition(!FirmwareVersion.isNewer("1.0.0", than: invalid))
        }
        print("ok: firmware upgrades, equality, downgrades and malformed versions")
    }
}
