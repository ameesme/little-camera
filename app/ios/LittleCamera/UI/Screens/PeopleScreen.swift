import SwiftUI

/// Who reads the little blog. Whoever is waiting sits at the top, because that is the
/// only thing on this screen that needs an answer. Swipe to be rid of anyone.
struct PeopleScreen: View {
    let blog: Blog
    @State private var adding = false

    var body: some View {
        NavigationStack {
            List {
                if blog.waiting.isEmpty && blog.approved.isEmpty {
                    Nobody()
                }
                if !blog.waiting.isEmpty {
                    Section {
                        ForEach(blog.waiting) { subscriber in
                            PendingRow(subscriber: subscriber, blog: blog)
                        }
                    } header: {
                        Caption("Asked to follow")
                    }
                }

                if !blog.approved.isEmpty {
                    Section {
                        ForEach(blog.approved) { subscriber in
                            PersonRow(subscriber: subscriber, blog: blog)
                        }
                    } header: {
                        Caption("Reading along")
                    }
                }
            }
            .listStyle(.plain)
            .environment(\.defaultMinListRowHeight, 56)
            .navigationTitle("People")
            .navigationBarTitleDisplayMode(.inline)
            .toolbar {
                ToolbarItem(placement: .topBarTrailing) {
                    Button { adding = true } label: { Image(systemName: "plus") }
                }
            }
            .sheet(isPresented: $adding) { AddPersonScreen(blog: blog) }
        }
    }
}

/// Nobody has asked and nobody has been added, so the only thing to say is how one gets here.
private struct Nobody: View {
    var body: some View {
        VStack(spacing: 10) {
            Text("Nobody here yet")
                .font(Theme.font(22, bold: true))
                .kerning(-0.6)
            Text("Share the address and whoever asks turns up here.")
                .font(Theme.body)
                .multilineTextAlignment(.center)
        }
        .frame(maxWidth: .infinity)
        .padding(.vertical, 60)
        .listRowSeparator(.hidden)
    }
}

/// Someone who asked to follow: one tap lets them in, a swipe turns them away.
private struct PendingRow: View {
    let subscriber: Subscriber
    let blog: Blog

    var body: some View {
        HStack(alignment: .firstTextBaseline) {
            Name(subscriber: subscriber)
            Spacer(minLength: 12)
            Button { blog.approve(subscriber) } label: { Caption("Let in") }
                .buttonStyle(SmallButton())
        }
        .swipe(subscriber, blog)
    }
}

private struct PersonRow: View {
    let subscriber: Subscriber
    let blog: Blog

    var body: some View {
        Name(subscriber: subscriber)
            .swipe(subscriber, blog)
    }
}

private struct Name: View {
    let subscriber: Subscriber

    var body: some View {
        VStack(alignment: .leading, spacing: 2) {
            Text(subscriber.name ?? subscriber.email)
            if subscriber.name != nil {
                Text(subscriber.email).font(Theme.small)
            }
        }
    }
}

private extension View {
    /// One way out, not three: whoever this is, they stop reading the blog.
    func swipe(_ subscriber: Subscriber, _ blog: Blog) -> some View {
        swipeActions(edge: .trailing) {
            Button(role: .destructive) { blog.remove(subscriber) } label: { Text("Remove") }
        }
    }
}

/// A name and an address, and the blog lets them in without asking them first.
struct AddPersonScreen: View {
    let blog: Blog
    @Environment(\.dismiss) private var dismiss
    @State private var email = ""
    @State private var name = ""

    var body: some View {
        NavigationStack {
            VStack(spacing: 0) {
                Spacer(minLength: 24)
                Text("Who is reading?")
                    .font(Theme.font(30, bold: true))
                    .kerning(-1)
                Text("They get an email with the address and nothing else.")
                    .font(Theme.lead)
                    .multilineTextAlignment(.center)
                    .padding(.horizontal, Theme.pad)
                    .padding(.top, 10)
                Field(placeholder: "them@example.com", text: $email, keyboard: .emailAddress, autofocus: true)
                    .padding(.horizontal, Theme.pad)
                    .padding(.top, 28)
                Field(placeholder: "Their name", text: $name)
                    .padding(.horizontal, Theme.pad)
                    .padding(.top, 20)
                Spacer(minLength: 24)
                Button {
                    blog.add(email: email, name: name)
                    dismiss()
                } label: {
                    Caption("Add and let in")
                }
                .buttonStyle(BlackButton())
                .disabled(!email.trimmed.contains("@"))
                .card()
            }
            .multilineTextAlignment(.center)
            .background(Ground())
            .toolbar {
                ToolbarItem(placement: .topBarLeading) {
                    Button { dismiss() } label: { Image(systemName: "chevron.left") }
                }
            }
        }
    }
}
