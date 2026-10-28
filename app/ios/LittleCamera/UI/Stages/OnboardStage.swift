import SwiftUI

/// No blog on this camera yet. Make one or sign in to one, let the camera photograph
/// the code on this screen, then let it take the picture that heads the blog.
struct OnboardStage: View {
    let model: AppModel

    enum Step {
        case choose
        case create
        case signIn
        case sent
        case bind
        case avatar
    }

    @State private var step: Step = .choose
    @State private var name = ""
    @State private var email = ""
    @State private var handle = ""

    var body: some View {
        Group {
            if step == .bind {
                bindScreen
            } else {
                DeviceStage(
                    title: title,
                    celebrate: model.blog.isLinked ? 1 : 0
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
    }

    private var title: String {
        switch step {
        case .choose: return "Ready to blog?"
        case .create: return "Make a blog"
        case .signIn, .sent: return "Sign in"
        case .bind: return ""
        case .avatar: return "One of you"
        }
    }

    @ViewBuilder private var content: some View {
        switch step {
        case .choose:
            Text("Get started with a new blog or continue one you already have")
        case .create:
            Field(placeholder: "Your name", text: $name)
            Field(placeholder: "you@example.com", text: $email, keyboard: .emailAddress)
            Field(placeholder: "handle", text: $handle)
            Text("\(handle.isEmpty ? "handle" : handle).\(model.serverURL.withoutScheme)")
                .font(Theme.small)
        case .signIn:
            Text("The address your blog is registered to.")
            Field(placeholder: "you@example.com", text: $email, keyboard: .emailAddress)
        case .sent:
            Text("Check your email. The link works for an hour.")
            Waiting(text: "Waiting for you")
        case .bind:
            EmptyView()
        case .avatar:
            Text(model.blog.awaitingAvatar
                 ? "Take one and press send."
                 : "Linked. Now a picture to head the blog.")
            if model.blog.awaitingAvatar {
                Waiting(text: "Waiting for the picture")
            }
        }
    }

    @ViewBuilder private var buttons: some View {
        switch step {
        case .choose:
            Button { step = .create } label: { Caption("Make a blog") }
                .buttonStyle(BlackButton())
            Button { step = .signIn } label: { Caption("I already have one") }
                .buttonStyle(QuietButton())
        case .create:
            Button { model.blog.createBlog(name: name, email: email, handle: handle) } label: {
                Caption("Make it")
            }
            .buttonStyle(BlackButton())
            .disabled(name.trimmed.isEmpty || email.trimmed.isEmpty || handle.trimmed.isEmpty)
            Button { step = .choose } label: { Caption("Back") }
                .buttonStyle(QuietButton())
        case .signIn:
            Button { model.blog.signIn(email: email); step = .sent } label: { Caption("Email me a link") }
                .buttonStyle(BlackButton())
                .disabled(email.trimmed.isEmpty)
            Button { step = .choose } label: { Caption("Back") }
                .buttonStyle(QuietButton())
        case .sent:
            Button { model.refresh(); step = .bind } label: { Caption("I opened it") }
                .buttonStyle(BlackButton())
        case .bind:
            EmptyView()
        case .avatar:
            if !model.blog.awaitingAvatar {
                Button { model.blog.requestAvatar() } label: { Caption("Take it with the camera") }
                    .buttonStyle(BlackButton())
            }
            Button { model.refresh() } label: { Caption(model.blog.awaitingAvatar ? "Done" : "Skip") }
                .buttonStyle(QuietButton())
        }
    }

    /// The code fills the screen, because the camera has to read it off this glass.
    private var bindScreen: some View {
        VStack(spacing: 0) {
            Spacer(minLength: 0)
            Text("Point the camera at this")
                .font(Theme.font(26, bold: true))
                .kerning(-0.5)
                .card()
            if let code = model.blog.verificationCode {
                QRView(code: code)
                    .padding(.horizontal, Theme.pad)
            } else {
                Dither().aspectRatio(1, contentMode: .fit).padding(.horizontal, Theme.pad)
            }
            Text("Holding the camera is the proof. Nothing is typed.")
                .font(Theme.small)
                .card()
            Waiting(text: "Waiting for the camera")
            Spacer(minLength: 0)
            Button { model.refresh() } label: { Caption("Check again") }
                .buttonStyle(QuietButton())
                .card()
        }
        .background(Theme.paper.ignoresSafeArea())
        .task { model.blog.fetchVerificationCode() }
    }
}
