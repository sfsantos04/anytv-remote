import SwiftUI

struct DevicesView: View {
    @EnvironmentObject private var store: DeviceStore
    @State private var manualHost = ""
    @State private var manualPort = "5555"
    @State private var scanning = false
    @State private var scanDone = 0
    @State private var message = ""
    @State private var showAdd = false

    var body: some View {
        NavigationStack {
            List {
                Section {
                    if store.devices.isEmpty {
                        Text("Nenhuma TV salva ainda.")
                            .foregroundStyle(.secondary)
                    }
                    ForEach(store.devices) { device in
                        NavigationLink(value: device) {
                            VStack(alignment: .leading, spacing: 2) {
                                Text(device.label).font(.headline)
                                Text("\(device.host):\(device.port)")
                                    .font(.caption)
                                    .foregroundStyle(.secondary)
                            }
                        }
                    }
                    .onDelete { offsets in store.remove(at: offsets) }
                } header: {
                    Text("Minhas TVs")
                }

                Section {
                    Button {
                        scan()
                    } label: {
                        HStack {
                            Label(scanning ? "Procurando..." : "Procurar na rede", systemImage: "wifi")
                            Spacer()
                            if scanning {
                                ProgressView()
                            } else if scanDone > 0 {
                                Text("\(scanDone)").foregroundStyle(.secondary)
                            }
                        }
                    }
                    .disabled(scanning)
                    Button {
                        showAdd = true
                    } label: {
                        Label("Adicionar por IP", systemImage: "plus.circle")
                    }
                } header: {
                    Text("Descobrir")
                } footer: {
                    Text("A TV precisa estar com a depuracao (ADB) ligada: Ajustes > Sobre > "
                         + "tocar 7x em Compilacao > Opcoes do desenvolvedor > Depuracao USB/rede.")
                }

                if !message.isEmpty {
                    Section {
                        Text(message).font(.footnote).foregroundStyle(.secondary)
                    }
                }
            }
            .navigationTitle("Controle TV")
            .navigationDestination(for: TVDevice.self) { device in
                RemoteView(device: device)
            }
            .sheet(isPresented: $showAdd) { addSheet }
        }
    }

    private var addSheet: some View {
        NavigationStack {
            Form {
                Section("Endereco da TV") {
                    TextField("192.168.0.10", text: $manualHost)
                        .keyboardType(.numbersAndPunctuation)
                        .autocorrectionDisabled()
                    TextField("Porta", text: $manualPort)
                        .keyboardType(.numberPad)
                }
            }
            .navigationTitle("Adicionar TV")
            .toolbar {
                ToolbarItem(placement: .cancellationAction) {
                    Button("Cancelar") { showAdd = false }
                }
                ToolbarItem(placement: .confirmationAction) {
                    Button("Salvar") {
                        let host = manualHost.trimmingCharacters(in: .whitespaces)
                        guard !host.isEmpty else { return }
                        let port = Int(manualPort) ?? 5555
                        store.upsert(TVDevice(name: host, host: host, port: port))
                        manualHost = ""
                        showAdd = false
                    }
                }
            }
        }
    }

    private func scan() {
        guard let base = NetworkInfo.subnetPrefix() else {
            message = "Nao consegui ler o IP do iPhone. Adicione a TV por IP."
            return
        }
        scanning = true
        scanDone = 0
        message = "Varrendo \(base).1 a \(base).254 na porta 5555..."
        let found = NSMutableArray()
        let lock = NSLock()
        Task.detached(priority: .userInitiated) {
            DispatchQueue.concurrentPerform(iterations: 254) { index in
                let host = "\(base).\(index + 1)"
                let ok = host.withCString { adb_probe($0, 5555, 350) } == 1
                if ok {
                    lock.lock()
                    found.add(host)
                    lock.unlock()
                }
            }
            let hosts = (found as? [String]) ?? []
            await MainActor.run {
                scanning = false
                scanDone = hosts.count
                for host in hosts {
                    store.upsert(TVDevice(name: host, host: host, port: 5555))
                }
                message = hosts.isEmpty
                    ? "Nenhum aparelho ADB encontrado. Ligue a depuracao na TV ou adicione por IP."
                    : "Encontrados: \(hosts.count). Toque na TV para conectar."
            }
        }
    }
}

enum NetworkInfo {
    /// "192.168.1" for a /24 network, nil when no Wi-Fi address is available.
    static func subnetPrefix() -> String? {
        var address: String?
        var ifaddr: UnsafeMutablePointer<ifaddrs>?
        guard getifaddrs(&ifaddr) == 0, let first = ifaddr else { return nil }
        defer { freeifaddrs(ifaddr) }
        var pointer: UnsafeMutablePointer<ifaddrs>? = first
        while let current = pointer {
            let interface = current.pointee
            if let addr = interface.ifa_addr, addr.pointee.sa_family == UInt8(AF_INET) {
                let name = String(cString: interface.ifa_name)
                if name == "en0" || name == "en1" {
                    var host = [CChar](repeating: 0, count: Int(NI_MAXHOST))
                    let result = getnameinfo(addr, socklen_t(addr.pointee.sa_len),
                                             &host, socklen_t(host.count), nil, 0,
                                             NI_NUMERICHOST)
                    if result == 0 {
                        address = String(cString: host)
                    }
                }
            }
            pointer = interface.ifa_next
        }
        guard let ip = address else { return nil }
        let parts = ip.split(separator: ".")
        guard parts.count == 4 else { return nil }
        return "\(parts[0]).\(parts[1]).\(parts[2])"
    }
}
