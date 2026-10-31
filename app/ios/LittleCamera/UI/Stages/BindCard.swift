import SwiftUI

/// The card little camera has to photograph, held up to it. The whole screen turns on its
/// side, because the camera sees 4:3 the long way and the card may as well have the room;
/// black ground and white card so there is no mistaking what to point at. The card is also
/// blank paper — whatever is drawn on it goes up as the blog's first picture.
struct BindCard: View {
    let code: String?
    let name: String
    let onCheck: () -> Void

    @State private var strokes: [[CGPoint]] = []
    @State private var drawing: [CGPoint] = []

    var body: some View {
        GeometryReader { geo in
            // Laid out at the landscape size and turned as one piece, so the writing and
            // the card agree about which way is up: the camera's way, not the phone's.
            let landscape = CGSize(width: geo.size.height, height: geo.size.width)
            ZStack {
                // The same vignette every other screen stands on, turned over with the rest of
                // the page: the ground is part of how this app looks, not a white-page habit.
                Ground().colorInvert()
                page(landscape)
                    .frame(width: landscape.width, height: landscape.height)
                    .rotationEffect(.degrees(90))
                    .position(x: geo.size.width / 2, y: geo.size.height / 2)
            }
        }
        .ignoresSafeArea()
        .statusBarHidden()
        // No button to press: the photo arriving is what ends this screen, so it asks on its own.
        .task {
            while !Task.isCancelled {
                try? await Task.sleep(for: .seconds(3))
                onCheck()
            }
        }
    }

    private func page(_ size: CGSize) -> some View {
        // Turning the page does not turn the island or the home indicator, which stay at the
        // two ends of what is now the long edge. 64 clears both without measuring either.
        let margin: CGFloat = 64
        let height = size.height - 40
        return HStack(alignment: .center, spacing: 28) {
            VStack(alignment: .leading, spacing: 10) {
                Text("Take a verification photo")
                    .font(Theme.font(26, bold: true))
                    .kerning(-0.8)
                Text("Photograph this card to pair your little camera to the little blog.")
                    .font(Theme.body)
                Text("Draw on it first if you like. This will be your blog\u{2019}s first picture!")
                    .font(Theme.body)
                Waiting(text: "Waiting for the photo", ink: Theme.paper)
                    .padding(.top, 4)
                HStack(spacing: 10) {
                    if !strokes.isEmpty {
                        Button { strokes = [] } label: { Caption("Clear") }
                            .buttonStyle(PaperButton())
                    }
                }
                .padding(.top, 8)
            }
            .foregroundStyle(Theme.paper)
            .frame(width: size.width - margin * 2 - 28 - height * 4 / 3, alignment: .leading)
            card(height: height)
        }
        .padding(.horizontal, margin)
    }

    /// 4:3, the shape of what little camera sees.
    private func card(height: CGFloat) -> some View {
        // The camera stores 320 px across, dithered to one bit, and jsQR needs about three and a half
        // of those per module to find a 21-module code. A card that fills half the frame leaves the
        // code about two thirds of the card before it has enough: this is the floor, not a preference.
        let qr = height * 0.68
        return ZStack {
            Theme.paper
            Canvas { context, _ in
                for stroke in strokes + [drawing] where stroke.count > 1 {
                    var path = Path()
                    path.addLines(stroke)
                    context.stroke(path, with: .color(Theme.ink),
                                   style: StrokeStyle(lineWidth: 6, lineCap: .round, lineJoin: .round))
                }
            }
            VStack(spacing: 0) {
                Spacer(minLength: 0)
                HStack(alignment: .bottom, spacing: 14) {
                    // Drawn last and never scribbled over: the code is the only part of this
                    // card that has to survive being decorated.
                    Group {
                        if let code {
                            QRView(code: code)
                        } else {
                            Dither()
                        }
                    }
                    .frame(width: qr, height: qr)
                    Text("this is \(owner)\nlittle camera")
                        .font(Theme.font(22, bold: true))
                        .kerning(-0.6)
                        .lineSpacing(-3)
                        .multilineTextAlignment(.leading)
                    Spacer(minLength: 0)
                }
                .padding(16)
            }
        }
        .frame(width: height * 4 / 3, height: height)
        .overlay(Rectangle().stroke(Theme.ink, lineWidth: 3))
        .contentShape(Rectangle())
        .gesture(
            DragGesture(minimumDistance: 0)
                .onChanged { drawing.append($0.location) }
                .onEnded { _ in
                    strokes.append(drawing)
                    drawing = []
                }
        )
    }

    private var owner: String {
        name.isEmpty ? "your" : "\(name.lowercased())\u{2019}s"
    }
}
