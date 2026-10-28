import SwiftUI

/// No blog on this camera yet. Make one or sign in to one, let little camera photograph
/// the code on this screen, then let it take the picture that heads the blog.
struct OnboardStage: View {
    let model: AppModel

    /// One question per screen, in the order the server needs the answers.
    enum Step: Equatable {
        case choose
        case name
        case email
        case handle
        case signIn
        case sent
        case bind
        case avatar
    }

    @State private var step: Step = Demo.step ?? .choose
    @State private var name = Demo.typedName
    @State private var email = ""
    @State private var handle = Demo.typedHandle

    var body: some View {
        Group {
            if step == .bind {
                bindScreen
            } else {
                DeviceStage(
                    title: title,
                    mood: mood,
                    turn: model.blog.isLinked ? 1 : 0,
                    back: back
                ) {
                    content
                    if let error = model.blog.error {
                        Text(error).font(Theme.small)
                    }
                } actions: {
                    buttons
                }
            }
        }
        .onChange(of: model.blog.isLinked) { _, linked in
            if linked { step = .avatar }
        }
        .onChange(of: model.blog.made?.handle) { _, made in
            if made != nil { step = .bind }
        }
        .onChange(of: model.blog.takenEmail) { _, taken in
            if taken { step = .signIn }
        }
        .onChange(of: step) { _, now in
            if now == .handle, handle.isEmpty { handle = Handle.filter(name) }
        }
    }

    private var mood: DeviceMood {
        switch step {
        case .choose, .name, .email, .handle, .signIn: return .images
        case .sent: return .mail
        case .bind: return .images
        case .avatar: return .camera
        }
    }

    private var title: String {
        switch step {
        case .choose: return "Start a little blog"
        case .name: return "Pick a name"
        case .email: return "Add your email"
        case .handle: return "Claim your little blog"
        case .signIn: return "Sign in"
        case .sent: return "Open the email"
        case .bind: return ""
        case .avatar: return "Take one of you"
        }
    }

    private var back: (() -> Void)? {
        switch step {
        case .choose, .avatar: return nil
        case .name, .signIn, .sent: return { step = .choose }
        case .email: return { step = .name }
        case .handle: return { step = .email }
        case .bind: return nil
        }
    }

    @ViewBuilder private var content: some View {
        switch step {
        case .choose:
            Text("Get started with a new little blog\nor continue one you already have.")
        case .name:
            Text("It goes under every picture you send.")
            Field(placeholder: "Your name", text: $name, autofocus: true)
        case .email:
            Text("The only way back in, so make it one you read.")
            Field(placeholder: "you@example.com", text: $email, keyboard: .emailAddress, autofocus: true)
        case .handle:
            AddressField(
                handle: handleBinding,
                suffix: ".\(model.serverURL.withoutScheme)",
                availability: model.blog.availability
            )
            .task(id: handle) {
                try? await Task.sleep(for: .milliseconds(400))
                await model.blog.checkHandle(handle)
            }
        case .signIn:
            Text(model.blog.takenEmail
                 ? "That address already has a blog. This is the way back into it."
                 : "The address your blog is registered to.")
            Field(placeholder: "you@example.com", text: $email, keyboard: .emailAddress, autofocus: true)
        case .sent:
            Text("The link works for an hour.")
            Waiting(text: "Waiting for you")
        case .bind:
            EmptyView()
        case .avatar:
            if model.blog.awaitingAvatar {
                Waiting(text: "Waiting for the picture")
            } else {
                Text("A picture to head your little blog.")
            }
        }
    }

    @ViewBuilder private var buttons: some View {
        switch step {
        case .choose:
            Button { step = .name } label: { Caption("Make a little blog") }
                .buttonStyle(BlackButton())
            Button { step = .signIn } label: { Caption("I already have one") }
                .buttonStyle(QuietButton())
        case .name:
            primary("Next", enabled: !name.trimmed.isEmpty) { step = .email }
        case .email:
            primary("Next", enabled: email.trimmed.contains("@")) { step = .handle }
        case .handle:
            primary("Make it", enabled: model.blog.availability == .free) {
                model.blog.createBlog(name: name, email: email, handle: handle)
            }
        case .signIn:
            primary("Email me a link", enabled: email.trimmed.contains("@")) {
                model.blog.signIn(email: email)
                step = .sent
            }
        case .sent:
            primary("Check again") { model.refresh(); step = .bind }
        case .bind:
            EmptyView()
        case .avatar:
            if model.blog.awaitingAvatar {
                primary("Done") { model.skipAvatar() }
            } else {
                primary("Take it with little camera") { model.blog.requestAvatar() }
            }
        }
    }

    /// Anything the address cannot contain never arrives, so there is nothing to tell anyone off about.
    private var handleBinding: Binding<String> {
        Binding(get: { handle }, set: { handle = Handle.filter($0) })
    }

    private func primary(_ label: String, enabled: Bool = true, action: @escaping () -> Void) -> some View {
        Button(action: action) { Caption(label) }
            .buttonStyle(BlackButton())
            .disabled(!enabled)
    }

    /// The code as little camera has to see it: a card inside its own 4:3 frame, held up to it.
    private var bindScreen: some View {
        VStack(spacing: 0) {
            Spacer(minLength: 0)
            Text("Take a photo with little camera")
                .font(Theme.font(30, bold: true))
                .kerning(-1)
                .frame(maxWidth: .infinity)
                .padding(.horizontal, Theme.pad)
            ScanCard(code: model.blog.verificationCode, name: cardName)
                .padding(.horizontal, Theme.pad)
                .padding(.top, 28)
            Waiting(text: "Waiting for the picture")
                .padding(.top, 36)
            Spacer(minLength: 0)
            Button { model.refresh() } label: { Caption("Check again") }
                .buttonStyle(BlackButton())
                .card()
        }
        .multilineTextAlignment(.center)
        .background(Theme.paper.ignoresSafeArea())
        .task { model.blog.fetchVerificationCode() }
    }

    private var cardName: String {
        let typed = name.trimmed
        if !typed.isEmpty { return typed }
        return model.blog.made?.handle ?? model.blog.bound?.handle ?? handle.trimmed
    }
}

/// A 4:3 card, the shape of what little camera sees, with the corners marked so it is
/// obvious where to aim. The name under the code says which blog is being joined.
private struct ScanCard: View {
    let code: String?
    let name: String

    /// Lower case, the way the wordmark is set.
    private var label: String {
        let owner = name.isEmpty ? "your" : "\(name.lowercased())\u{2019}s"
        return "this is \(owner)\nlittle camera"
    }

    var body: some View {
        HStack(spacing: 20) {
            Group {
                if let code {
                    QRView(code: code)
                } else {
                    Dither().aspectRatio(1, contentMode: .fit)
                }
            }
            Text(label)
                .font(Theme.font(26, bold: true))
                .kerning(-0.8)
                .lineSpacing(-4)
                .frame(maxWidth: .infinity, alignment: .leading)
                .multilineTextAlignment(.leading)
        }
        .padding(20)
        .frame(maxWidth: .infinity)
        .aspectRatio(4.0 / 3.0, contentMode: .fit)
        .overlay(Corners().stroke(Theme.ink, lineWidth: 2))
    }
}

/// The address, typed in place: what is typed and what is fixed on one line, with the
/// answer to "is it free" beside it rather than after the fact.
private struct AddressField: View {
    @Binding var handle: String
    let suffix: String
    let availability: Blog.Availability
    @FocusState private var focused: Bool

    var body: some View {
        VStack(spacing: 4) {
            HStack(spacing: 0) {
                TextField("you", text: $handle)
                    .font(Theme.font(26, bold: true))
                    .kerning(-0.5)
                    .fixedSize()
                    .textFieldStyle(.plain)
                    .textInputAutocapitalization(.never)
                    .autocorrectionDisabled()
                    .tint(Theme.ink)
                    .focused($focused)
                    .task { focused = true }
                Text(suffix)
                    .font(Theme.font(26))
                    .kerning(-0.5)
            }
            .frame(maxWidth: .infinity)
            Theme.ink.frame(height: 2)
            verdict
                .frame(height: 28)
        }
    }

    @ViewBuilder private var verdict: some View {
        switch availability {
        case .unknown:
            Color.clear
        case .checking:
            HStack(spacing: 10) {
                Spinner()
                Text("Checking").font(Theme.body)
            }
        case .free:
            mark("checkmark", "Available")
        case .taken:
            mark("xmark", "Taken")
        }
    }

    private func mark(_ symbol: String, _ label: String) -> some View {
        HStack(spacing: 8) {
            Image(systemName: symbol).font(.system(size: 15, weight: .bold))
            Text(label).font(Theme.body)
        }
    }
}

/// Four crop marks, the way a viewfinder draws them.
private struct Corners: Shape {
    var length: CGFloat = 26

    func path(in rect: CGRect) -> Path {
        var path = Path()
        for x in [rect.minX, rect.maxX] {
            for y in [rect.minY, rect.maxY] {
                let dx: CGFloat = x == rect.minX ? length : -length
                let dy: CGFloat = y == rect.minY ? length : -length
                path.move(to: CGPoint(x: x + dx, y: y))
                path.addLine(to: CGPoint(x: x, y: y))
                path.addLine(to: CGPoint(x: x, y: y + dy))
            }
        }
        return path
    }
}
