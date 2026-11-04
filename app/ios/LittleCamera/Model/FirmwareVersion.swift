enum FirmwareVersion {
    static func isNewer(_ offered: String, than installed: String) -> Bool {
        guard let candidate = components(offered), let current = components(installed) else { return false }
        return current.lexicographicallyPrecedes(candidate)
    }

    private static func components(_ version: String) -> [UInt]? {
        let parts = version.split(separator: ".", omittingEmptySubsequences: false)
        guard parts.count == 3 else { return nil }
        var result: [UInt] = []
        for part in parts {
            guard !part.isEmpty, part.utf8.allSatisfy({ $0 >= 48 && $0 <= 57 }),
                  let value = UInt(part) else { return nil }
            result.append(value)
        }
        return result
    }
}
