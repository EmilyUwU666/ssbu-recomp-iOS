import MetalKit
import SwiftUI

/// Minimal iOS-native GPU presentation path; unrelated to the Switch NVN backend.
struct MetalBootstrapSurface: UIViewRepresentable {
    func makeCoordinator() -> Renderer {
        Renderer()
    }

    func makeUIView(context: Context) -> MTKView {
        let view = MTKView(frame: .zero, device: context.coordinator.device)
        view.colorPixelFormat = .bgra8Unorm
        view.clearColor = MTLClearColor(red: 0.08, green: 0.15, blue: 0.27, alpha: 1.0)
        view.isOpaque = true
        view.preferredFramesPerSecond = 30
        view.isPaused = false
        view.enableSetNeedsDisplay = false
        view.delegate = context.coordinator
        return view
    }

    func updateUIView(_ uiView: MTKView, context: Context) {}

    final class Renderer: NSObject, MTKViewDelegate {
        let device: MTLDevice? = MTLCreateSystemDefaultDevice()
        private lazy var queue: MTLCommandQueue? = device?.makeCommandQueue()

        func mtkView(_ view: MTKView, drawableSizeWillChange size: CGSize) {}

        func draw(in view: MTKView) {
            guard let pass = view.currentRenderPassDescriptor,
                  let drawable = view.currentDrawable,
                  let buffer = queue?.makeCommandBuffer(),
                  let encoder = buffer.makeRenderCommandEncoder(descriptor: pass) else {
                return
            }
            encoder.endEncoding()
            buffer.present(drawable)
            buffer.commit()
        }
    }
}
