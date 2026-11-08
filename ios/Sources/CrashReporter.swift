import Foundation

// Grava um bilhete quando o app cai por sinal (SIGSEGV, SIGABRT, SIGBUS).
// Sem isso, um fechamento no aparelho do usuário não deixa rastro nenhum.

enum CrashReporter {
    private static var fd: Int32 = -1
    static var logURL: URL {
        FileManager.default.urls(for: .documentDirectory, in: .userDomainMask)[0]
            .appendingPathComponent("anytv-crash.log")
    }

    static func install() {
        let path = logURL.path
        fd = open(path, O_CREAT | O_WRONLY | O_APPEND, 0o644)
        signal(SIGSEGV, handle)
        signal(SIGBUS, handle)
        signal(SIGABRT, handle)
        signal(SIGILL, handle)
        signal(SIGFPE, handle)
    }

    /// Chamado pelo handler; escreve direto no descritor, sem alocar.
    static func write(_ text: String) {
        guard fd >= 0 else { return }
        var stamp = time(nil)
        var tmv = tm()
        localtime_r(&stamp, &tmv)
        var buffer = [CChar](repeating: 0, count: 256)
        let n = strftime(&buffer, buffer.count, "%Y-%m-%dT%H:%M:%S ", &tmv)
        if n > 0 { _ = Darwin.write(fd, buffer, n) }
        text.withCString { _ = Darwin.write(fd, $0, Int(strlen($0))) }
        _ = Darwin.write(fd, "\n", 1)
        fsync(fd)
    }

    static func read() -> String {
        (try? String(contentsOf: logURL, encoding: .utf8)) ?? ""
    }

    static func clear() {
        try? FileManager.default.removeItem(at: logURL)
    }
}

private func handle(_ sig: Int32) {
    let name: String
    switch sig {
    case SIGSEGV: name = "SIGSEGV (acesso invalido)"
    case SIGBUS: name = "SIGBUS"
    case SIGABRT: name = "SIGABRT (travamento do runtime)"
    case SIGILL: name = "SIGILL"
    case SIGFPE: name = "SIGFPE"
    default: name = "sinal \(sig)"
    }
    CrashReporter.write("CAIU: \(name)")
    // devolve o sinal ao sistema para o iOS encerrar normalmente
    signal(sig, SIG_DFL)
    raise(sig)
}
