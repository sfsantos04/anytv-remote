import SwiftUI

struct DevicesView: View {
    @EnvironmentObject private var store: DeviceStore
    @State private var manualHost = ""
    @State private var manualPort = ""
    @State private var manualProto: TVProto = .adb
    @State private var scanning = false
    @State private var found = 0
    @State private var message = ""
    @State private var showAdd = false
    @State private var crashNote = ""

    var body: some View {
        NavigationStack {
            List {
                if !crashNote.isEmpty {
                    Section {
                        VStack(alignment: .leading, spacing: 6) {
                            Label("O app fechou na última vez", systemImage: "exclamationmark.triangle.fill")
                                .font(.subheadline.bold())
                                .foregroundStyle(.orange)
                            Text(crashNote)
                                .font(.caption)
                                .foregroundStyle(.secondary)
                                .textSelection(.enabled)
                            Button("Dispensar") {
                                CrashReporter.clear()
                                crashNote = ""
                            }
                            .font(.caption)
                        }
                        .padding(.vertical, 4)
                    }
                }
                Section {
                    if store.devices.isEmpty {
                        Text("Nenhuma TV salva ainda.").foregroundStyle(.secondary)
                    }
                    ForEach(store.devices) { device in
                        NavigationLink(value: device) {
                            HStack {
                                VStack(alignment: .leading, spacing: 2) {
                                    Text(device.label).font(.headline)
                                    Text("\(device.host):\(device.port) - \(device.proto.title)")
                                        .font(.caption)
                                        .foregroundStyle(.secondary)
                                }
                                Spacer()
                                Text(device.proto.short)
                                    .font(.caption2)
                                    .padding(.horizontal, 8)
                                    .padding(.vertical, 4)
                                    .background(Color.accentColor.opacity(0.18),
                                                in: Capsule())
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
                            if scanning { ProgressView() }
                            else if found > 0 { Text("\(found)").foregroundStyle(.secondary) }
                        }
                    }
                    .disabled(scanning)
                    Button {
                        showAdd = true
                    } label: {
                        Label("Adicionar manualmente", systemImage: "plus.circle")
                    }
                } header: {
                    Text("Descobrir")
                } footer: {
                    Text("A varredura testa Android (5555), Roku (8060), LG webOS (3000) "
                         + "e Samsung Tizen (8001) em cada endereco da rede.")
                }

                if !message.isEmpty {
                    Section {
                        Text(message).font(.footnote).foregroundStyle(.secondary)
                    }
                }
            }
            .task {
                let crash = CrashReporter.read()
                if !crash.isEmpty { crashNote = crash.trimmingCharacters(in: .whitespacesAndNewlines) }
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
                Section("TV") {
                    Picker("Protocolo", selection: $manualProto) {
                        ForEach(TVProto.allCases) { proto in
                            Text(proto.title).tag(proto)
                        }
                    }
                    TextField("192.168.0.10", text: $manualHost)
                        .keyboardType(.numbersAndPunctuation)
                        .autocorrectionDisabled()
                    TextField("porta (vazio = \(manualProto.port))", text: $manualPort)
                        .keyboardType(.numberPad)
                }
                Section {
                    Text(manualProto.setup).font(.footnote).foregroundStyle(.secondary)
                }
            }
            .task {
                let crash = CrashReporter.read()
                if !crash.isEmpty { crashNote = crash.trimmingCharacters(in: .whitespacesAndNewlines) }
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
                        let port = Int(manualPort) ?? manualProto.port
                        store.upsert(TVDevice(name: host, host: host, port: port,
                                              proto: manualProto))
                        manualHost = ""
                        manualPort = ""
                        showAdd = false
                    }
                }
            }
        }
    }

    private func scan() {
        guard let base = NetworkInfo.subnetPrefix() else {
            message = "Nao consegui ler o IP do iPhone. Adicione a TV pelo endereco."
            return
        }
        scanning = true
        found = 0
        message = "Varrendo \(base).1 a \(base).254..."
        let protocols = TVProto.allCases
        let lock = NSLock()
        var hits: [TVDevice] = []
        Task.detached(priority: .userInitiated) {
            DispatchQueue.concurrentPerform(iterations: 254) { index in
                let host = "\(base).\(index + 1)"
                for proto in protocols {
                    let ok = host.withCString { cHost in
                        anytv_probe_port(cHost, Int32(proto.port), 350) == 1
                    }
                    if ok {
                        lock.lock()
                        hits.append(TVDevice(name: host, host: host,
                                             port: proto.port, proto: proto))
                        lock.unlock()
                        return
                    }
                }
            }
            let devices = hits
            await MainActor.run {
                scanning = false
                found = devices.count
                for device in devices { store.upsert(device) }
                message = devices.isEmpty
                    ? "Nada encontrado. Confira se a TV esta ligada e na mesma rede."
                    : "Encontrados: \(devices.count). Toque para conectar."
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
                    if result == 0 { address = String(cString: host) }
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
