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


/// Tokens (Samsung token, webOS client-key) guardados por IP, fora do registro do
/// aparelho: se a lista de dispositivos for recriada, o pareamento nao se perde e
/// a TV deixa de pedir "Permitir" a cada uso.
enum TokenVault {
    private static func account(_ host: String, _ proto: TVProto) -> String {
        "token.\(proto.rawValue).\(host)"
    }

    static func load(host: String, proto: TVProto) -> String {
        let fromKeychain = Keychain.loadString(account(host, proto))
        if !fromKeychain.isEmpty { return fromKeychain }
        return UserDefaults.standard.string(forKey: account(host, proto)) ?? ""
    }

    static func save(_ token: String, host: String, proto: TVProto) {
        guard !token.isEmpty else { return }
        Keychain.saveString(token, account: account(host, proto))
        UserDefaults.standard.set(token, forKey: account(host, proto))
    }
}

/// Blocking C client (ADB, Roku ECP, webOS, Tizen) driven by a serial queue.
final class TVClient: ObservableObject, @unchecked Sendable {
    private let queue = DispatchQueue(label: "com.samflix.anytv.client")
    private var handle: OpaquePointer?

    let proto: TVProto
    private let host: String

    init(proto: TVProto, host: String = "", token: String = "", mac: String = "") {
        self.proto = proto
        self.host = host
        handle = anytv_client_new_proto(Int32(proto.rawValue))
        if let handle {
            if proto == .adb { loadOrGenerateKey(handle) }
            let effective = token.isEmpty ? TokenVault.load(host: host, proto: proto) : token
            if !effective.isEmpty {
                effective.withCString { anytv_client_set_token(handle, $0) }
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

    /// Asks the transport whether it can list apps (on Samsung this opens the dev
    /// bridge on 26101) and updates the capability mask accordingly.
    func probeApps() async -> Bool {
        (try? await run { [self] in
            guard let handle else { return false }
            return anytv_client_probe_apps(handle) != 0
        }) ?? false
    }

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
            return Data(buffer[0..<Int(got)])
        }
    }

    // MARK: - Live screen (H.264 over the ADB shell pipe)

    private var streamBuffer = [UInt8](repeating: 0, count: 192 * 1024)

    /// Starts scrcpy: pushes the bundled server jar to the TV, runs it there and
    /// opens the video socket. Better frame rate than screenrecord and works from
    /// Android 5.0 up.
    func scrcpyStart(bitrate: Int = 8_000_000, maxFPS: Int = 60, maxSize: Int = 0,
                     seconds: Int = 180) async throws -> (width: Int, height: Int) {
        guard let jarURL = Bundle.main.url(forResource: "scrcpy-server", withExtension: "jar"),
              let jar = try? Data(contentsOf: jarURL) else {
            throw TVClientError.failure("scrcpy-server.jar não está no app")
        }
        return try await run { [self] in
            guard let handle else { throw TVClientError.notConnected }
            let result: (Int32, Int32) = jar.withUnsafeBytes { raw in
                let base = raw.bindMemory(to: UInt8.self).baseAddress
                let rc = anytv_client_scrcpy_start(handle, base, jar.count,
                                                   Int32(bitrate), Int32(maxFPS), Int32(maxSize), 20000)
                if rc != 0 { return (-1, -1) }
                return (anytv_client_scrcpy_width(handle), anytv_client_scrcpy_height(handle))
            }
            if result.0 < 0 { throw TVClientError.failure(message()) }
            return (Int(result.0), Int(result.1))
        }
    }

    /// Starts the device encoder. Native resolution unless width/height are set.
    func streamStart(width: Int = 0, height: Int = 0,
                     bitrate: Int = 4_000_000, seconds: Int = 180) async throws {
        try await run { [self] in
            guard let handle else { throw TVClientError.notConnected }
            let rc = anytv_client_stream_start(handle, Int32(width), Int32(height),
                                               Int32(bitrate), Int32(seconds))
            if rc != 0 { throw TVClientError.failure(message()) }
        }
    }

    /// Returns the next chunk of the stream, an empty Data on a quiet moment
    /// (read timeout) and nil when the encoder stopped.
    func streamRead(timeoutMS: Int32 = 1200) async throws -> Data? {
        try await run { [self] in
            guard let handle else { throw TVClientError.notConnected }
            var result: Data?
            streamBuffer.withUnsafeMutableBufferPointer { buffer in
                let got = anytv_client_stream_read(handle, buffer.baseAddress, buffer.count, timeoutMS)
                if got > 0 {
                    result = Data(buffer[0..<Int(got)])
                } else if got == -5 {
                    result = Data()
                } else {
                    result = nil
                }
            }
            return result
        }
    }

    func streamStop() async {
        _ = try? await run { [self] in
            guard let handle else { return }
            anytv_client_stream_stop(handle)
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
