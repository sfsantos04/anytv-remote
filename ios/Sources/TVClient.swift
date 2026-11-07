import Foundation

enum TVClientError: LocalizedError {
    case notConnected
    case failure(String)

    var errorDescription: String? {
        switch self {
        case .notConnected:
            return "TV desconectada"
        case .failure(let message):
            return message
        }
    }
}

/// Blocking C client driven from a private serial queue; the UI never blocks.
final class TVClient: ObservableObject, @unchecked Sendable {
    private let queue = DispatchQueue(label: "com.samflix.anytv.client")
    private var handle: OpaquePointer?

    init() {
        handle = anytv_client_new()
        loadOrGenerateKey()
    }

    deinit {
        if let handle {
            anytv_client_disconnect(handle)
            anytv_client_free(handle)
        }
    }

    private func loadOrGenerateKey() {
        guard let handle else { return }
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
                anytv_client_connect(handle, cHost, Int32(port), 8000, 90000)
            }
            if rc != 0 {
                throw TVClientError.failure(message())
            }
            let model = String(cString: anytv_client_model(handle))
            let android = String(cString: anytv_client_android(handle))
            return (model, android)
        }
    }

    func disconnect() async {
        _ = try? await run { [self] in
            if let handle { anytv_client_disconnect(handle) }
        }
    }

    func key(_ code: Int, repeat count: Int = 1) async throws {
        try await run { [self] in
            guard let handle else { throw TVClientError.notConnected }
            let rc = anytv_client_key(handle, Int32(code), Int32(count))
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

    func apps() async throws -> [String] {
        try await run { [self] in
            guard let handle else { throw TVClientError.notConnected }
            var buffer = [CChar](repeating: 0, count: 64 * 1024)
            let rc = buffer.withUnsafeMutableBufferPointer { out in
                anytv_client_apps(handle, out.baseAddress, out.count)
            }
            if rc != 0 { throw TVClientError.failure(message()) }
            let raw = String(cString: buffer)
            var seen = Set<String>()
            var result: [String] = []
            for line in raw.split(separator: "\n") {
                var item = String(line).trimmingCharacters(in: .whitespaces)
                if item.hasPrefix("package:") { item = String(item.dropFirst(8)) }
                if let slash = item.firstIndex(of: "/") { item = String(item[..<slash]) }
                if item.isEmpty || seen.contains(item) { continue }
                if !item.contains(".") { continue }
                seen.insert(item)
                result.append(item)
            }
            return result.sorted()
        }
    }

    func launch(_ package: String) async throws {
        try await run { [self] in
            guard let handle else { throw TVClientError.notConnected }
            let rc = package.withCString { anytv_client_launch(handle, $0) }
            if rc != 0 { throw TVClientError.failure(message()) }
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
