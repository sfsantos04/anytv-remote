import Foundation

enum TVClientError: LocalizedError {
    case notConnected
    case unsupported(String)
    case failure(String)

    var errorDescription: String? {
        switch self {
        case .notConnected:
            return "TV desconectada"
        case .unsupported(let what):
            return what
        case .failure(let message):
            return message
        }
    }
}

/// Blocking C client (ADB, Roku ECP, webOS, Tizen) driven from a serial queue.
final class TVClient: ObservableObject, @unchecked Sendable {
    private let queue = DispatchQueue(label: "com.samflix.anytv.client")
    private var handle: OpaquePointer?

    let proto: TVProto

    init(proto: TVProto, token: String = "", mac: String = "") {
        self.proto = proto
        handle = anytv_client_new_proto(Int32(proto.rawValue))
        if let handle {
            if proto == .adb { loadOrGenerateKey(handle) }
            if !token.isEmpty {
                token.withCString { anytv_client_set_token(handle, $0) }
            }
            if !mac.isEmpty {
                mac.withCString { anytv_client_set_mac(handle, $0) }
            }
        }
    }

    deinit {
        if let handle {
            anytv_client_disconnect(handle)
            anytv_client_free(handle)
        }
    }

    private func loadOrGenerateKey(_ handle: OpaquePointer) {
        if let saved = Keychain.loadKey(), saved.count == Int(ANYTV_KEY_SIZE) {
            let loaded = saved.withUnsafeBytes { buffer -> Int32 in
                guard let base = buffer.bindMemory(to: UInt8.self).baseAddress else { return -1 }
                return anytv_client_key_load(handle, base, saved.count)
            }
            if loaded == 0 { return }
        }
        if anytv_client_keygen(handle) == 0 {
            var blob = Data(count: Int(ANYTV_KEY_SIZE))
            blob.withUnsafeMutableBytes { buffer in
                guard let base = buffer.bindMemory(to: UInt8.self).baseAddress else { return }
                _ = anytv_client_key_export(handle, base, Int(ANYTV_KEY_SIZE))
            }
            Keychain.saveKey(blob)
        }
    }

    private func message() -> String {
        guard let handle else { return "cliente indisponivel" }
        let text = String(cString: anytv_client_error(handle))
        return text.isEmpty ? "falha desconhecida" : text
    }

    var caps: UInt32 {
        guard let handle else { return 0 }
        return anytv_client_caps(handle)
    }

    func supports(_ capability: UInt32) -> Bool { caps & capability != 0 }

    func storedToken() -> String {
        guard let handle else { return "" }
        return String(cString: anytv_client_token(handle))
    }

    func discoveredMac() -> String {
        guard let handle else { return "" }
        return String(cString: anytv_client_mac(handle))
    }

    private func run<T>(_ body: @escaping () throws -> T) async throws -> T {
        try await withCheckedThrowingContinuation { (continuation: CheckedContinuation<T, Error>) in
            queue.async {
                do { continuation.resume(returning: try body()) }
                catch { continuation.resume(throwing: error) }
            }
        }
    }

    func connect(host: String, port: Int) async throws -> (model: String, android: String) {
        try await run { [self] in
            guard let handle else { throw TVClientError.failure("cliente indisponivel") }
            let rc = host.withCString { cHost in
                anytv_client_connect(handle, cHost, Int32(port), 6000, 90000)
            }
            if rc != 0 { throw TVClientError.failure(message()) }
            return (String(cString: anytv_client_model(handle)),
                    String(cString: anytv_client_android(handle)))
        }
    }

    func disconnect() async {
        _ = try? await run { [self] in
            if let handle { anytv_client_disconnect(handle) }
        }
    }

    func send(_ cmd: Cmd, repeat count: Int = 1) async throws {
        try await run { [self] in
            guard let handle else { throw TVClientError.notConnected }
            let rc = anytv_client_cmd(handle, Int32(cmd.rawValue), Int32(count))
            if rc != 0 { throw TVClientError.failure(message()) }
        }
    }

    func text(_ value: String) async throws {
        try await run { [self] in
            guard let handle else { throw TVClientError.notConnected }
            let rc = value.withCString { anytv_client_text(handle, $0) }
            if rc != 0 { throw TVClientError.failure(message()) }
        }
    }

    func shell(_ command: String) async throws -> String {
        try await run { [self] in
            guard let handle else { throw TVClientError.notConnected }
            var buffer = [CChar](repeating: 0, count: 64 * 1024)
            let rc = command.withCString { cmd in
                buffer.withUnsafeMutableBufferPointer { out in
                    anytv_client_shell(handle, cmd, out.baseAddress, out.count, 20000)
                }
            }
            if rc < 0 { throw TVClientError.failure(message()) }
            return String(cString: buffer)
        }
    }

    /// Returns (app id, display name) pairs.
    func apps() async throws -> [TVApp] {
        try await run { [self] in
            guard let handle else { throw TVClientError.notConnected }
            var buffer = [CChar](repeating: 0, count: 128 * 1024)
            let rc = buffer.withUnsafeMutableBufferPointer { out in
                anytv_client_apps(handle, out.baseAddress, out.count)
            }
            if rc != 0 { throw TVClientError.failure(message()) }
            let raw = String(cString: buffer)
            var seen = Set<String>()
            var result: [TVApp] = []
            for line in raw.split(separator: "\n") {
                var fields = String(line).components(separatedBy: "\t")
                guard let first = fields.first, !first.isEmpty else { continue }
                if first.hasPrefix("package:") {
                    fields[0] = String(first.dropFirst(8))
                }
                let identifier = fields[0].trimmingCharacters(in: .whitespaces)
                guard !identifier.isEmpty, !seen.contains(identifier) else { continue }
                seen.insert(identifier)
                let name = fields.count > 1 ? fields[1] : ""
                let iconText = fields.count > 2 ? fields[2] : ""
                let icon = iconText.isEmpty ? nil : URL(string: iconText)
                result.append(TVApp(id: identifier,
                                    name: name.isEmpty ? identifier : name,
                                    icon: icon))
            }
            return result.sorted { $0.name.localizedCaseInsensitiveCompare($1.name) == .orderedAscending }
        }
    }

    func launch(_ appID: String) async throws {
        try await run { [self] in
            guard let handle else { throw TVClientError.notConnected }
            let rc = appID.withCString { anytv_client_launch(handle, $0) }
            if rc != 0 { throw TVClientError.failure(message()) }
        }
    }

    /// Icon bytes pulled straight from the device (ADB only): the TV extracts the
    /// PNG out of the APK with busybox, so this stays a few dozen kilobytes even
    /// for a 200 MB app. Returns nil when the APK has no usable artwork.
    func appIcon(_ appID: String) async throws -> Data? {
        try await run { [self] in
            guard let handle else { throw TVClientError.notConnected }
            let capacity = 4 * 1024 * 1024
            var buffer = [UInt8](repeating: 0, count: capacity)
            let got = buffer.withUnsafeMutableBufferPointer { out in
                appID.withCString { anytv_client_app_icon(handle, $0, out.baseAddress, out.count) }
            }
            guard got > 0 else { return nil }
            return Data(buffer[0..<got])
        }
    }

    func info() async throws -> String {
        try await run { [self] in
            guard let handle else { throw TVClientError.notConnected }
            var buffer = [CChar](repeating: 0, count: 8192)
            let rc = buffer.withUnsafeMutableBufferPointer { out in
                anytv_client_info(handle, out.baseAddress, out.count)
            }
            if rc != 0 { throw TVClientError.failure(message()) }
            return String(cString: buffer)
        }
    }

    func screenshot() async throws -> Data {
        try await run { [self] in
            guard let handle else { throw TVClientError.notConnected }
            let capacity = 12 * 1024 * 1024
            var data = Data(count: capacity)
            let count = data.withUnsafeMutableBytes { buffer -> Int32 in
                guard let base = buffer.bindMemory(to: UInt8.self).baseAddress else { return -1 }
                return anytv_client_screenshot(handle, base, capacity)
            }
            if count <= 0 { throw TVClientError.failure(message()) }
            return data.prefix(Int(count))
        }
    }

    func powerOn() async throws {
        try await run { [self] in
            guard let handle else { throw TVClientError.notConnected }
            if anytv_client_power_on(handle) != 0 { throw TVClientError.failure(message()) }
        }
    }

    func wake() async throws {
        try await run { [self] in
            guard let handle else { throw TVClientError.notConnected }
            if anytv_client_wake(handle) != 0 { throw TVClientError.failure(message()) }
        }
    }

    func sleep() async throws {
        try await run { [self] in
            guard let handle else { throw TVClientError.notConnected }
            if anytv_client_sleep(handle) != 0 { throw TVClientError.failure(message()) }
        }
    }
}
