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
    private let failures = Int(ssbu_portability_selftest())

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
                        failures == 0 ? "Portable runtime checks passed" : "Runtime checks failed: \(failures)",
                        systemImage: failures == 0 ? "checkmark.circle.fill" : "xmark.circle.fill"
                    )
                    .foregroundStyle(failures == 0 ? .green : .red)
                    .font(.headline)

                    Text("This app is an early engineering scaffold. It tests portable C helpers from the upstream recompilation runtime. It does not load or run Super Smash Bros. Ultimate.")
                        .fixedSize(horizontal: false, vertical: true)

                    Divider()

                    Text("Remaining porting work").font(.headline)
                    Text("Guest memory mapping, a static game-code link, module loading, Metal/MoltenVK rendering, audio/video, file services, and input integration.")
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
