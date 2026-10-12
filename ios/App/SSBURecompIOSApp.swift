import SwiftUI

@main
struct SSBURecompIOSApp: App {
    var body: some Scene {
        WindowGroup {
            PortStatusView()
        }
    }
}

private struct PortStatusView: View {
    private let failures = Int(ssbu_portability_selftest()) | Int(ssbu_guest_memory_selftest()) | Int(ssbu_translated_memory_selftest()) | Int(ssbu_relative_relocation_selftest()) | Int(ssbu_guest_elf_loader_selftest()) | Int(ssbu_guest_module_import_selftest())

    var body: some View {
        NavigationStack {
            ScrollView {
                VStack(alignment: .leading, spacing: 18) {
                    Text("SSBU recomp · iOS")
                        .font(.largeTitle.bold())
                        .accessibilityAddTraits(.isHeader)
                    Text("Native ARM64 bootstrap")
                        .font(.title3)
                        .foregroundStyle(.secondary)

                    Label(
                        failures == 0 ? "Portable runtime and static dispatch checks passed" : "Runtime checks failed: \(failures)",
                        systemImage: failures == 0 ? "checkmark.circle.fill" : "xmark.circle.fill"
                    )
                    .foregroundStyle(failures == 0 ? .green : .red)
                    .font(.headline)

                    MetalBootstrapSurface()
                        .frame(height: 170)
                        .clipShape(RoundedRectangle(cornerRadius: 14))
                        .overlay(alignment: .bottomLeading) {
                            Text("Metal display test · no game graphics")
                                .font(.caption.weight(.semibold))
                                .foregroundStyle(.white)
                                .padding(12)
                        }

                    Text("This app statically links synthetic lifted C code, dispatches it through a function table against relocated guest memory, and displays a Metal test surface. It does not load or run Super Smash Bros. Ultimate.")
                        .fixedSize(horizontal: false, vertical: true)

                    Divider()

                    Text("Remaining porting work").font(.headline)
                    Text("Full guest memory integration, static game-code linking, module loading, NVN-to-Metal/Vulkan graphics, audio/video, file services, and input remain to be implemented.")
                        .foregroundStyle(.secondary)

                    Text("No game files, firmware, or keys are included.")
                        .font(.footnote)
                        .foregroundStyle(.secondary)
                }
                .padding(24)
                .frame(maxWidth: 650, alignment: .leading)
            }
            .navigationTitle("iOS port")
        }
    }
}
