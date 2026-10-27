import SwiftUI

/// Who can read the blog. Whoever is waiting comes first, because they are the only thing here needing a decision.
struct PeopleScreen: View {
    let blog: Blog
    @State private var addingByEmail = false

    var body: some View {
        VStack(alignment: .leading, spacing: 0) {
            if !blog.waiting.isEmpty {
                VStack(alignment: .leading, spacing: 10) {
                    Caption("Waiting to be let in")
                    ForEach(blog.waiting) { subscriber in
                        HStack(alignment: .firstTextBaseline, spacing: 8) {
                            SubscriberLabel(subscriber: subscriber)
                            Spacer(minLength: 4)
                            Button { blog.approve(subscriber) } label: { Caption("Approve") }
                                .buttonStyle(BlackButton())
                            Button { blog.block(subscriber) } label: { Caption("Block") }
                                .buttonStyle(BlackButton())
                        }
                    }
                }
                .card()
                Rule()
            }
            VStack(alignment: .leading, spacing: 10) {
                HStack {
                    Caption("Subscribers")
                    Spacer()
                    Button { addingByEmail = true } label: { Caption("Add by email") }
                        .buttonStyle(BlackButton())
                }
                if blog.approved.isEmpty {
                    Text("Nobody yet.").font(Theme.small)
                } else {
                    ForEach(blog.approved) { subscriber in
                        SubscriberLabel(subscriber: subscriber)
                    }
                }
                if let error = blog.error {
                    Text(error).font(Theme.small)
                }
            }
            .card()
            Rule()
        }
        .sheet(isPresented: $addingByEmail) {
            AddSubscriberSheet(blog: blog)
        }
    }
}

struct SubscriberLabel: View {
    let subscriber: Subscriber

    var body: some View {
        VStack(alignment: .leading, spacing: 2) {
            Text(subscriber.email).lineLimit(1)
            if let name = subscriber.name, !name.isEmpty {
                Text(name).font(Theme.small)
            }
        }
    }
}

struct AddSubscriberSheet: View {
    let blog: Blog
    @Environment(\.dismiss) private var dismiss
    @State private var email = ""
    @State private var name = ""

    private var isValid: Bool {
        email.contains("@") && email.contains(".")
    }

    var body: some View {
        VStack(alignment: .leading, spacing: 18) {
            Caption("Add by email")
            Text("They are approved right away and get a welcome link.")
                .font(Theme.small)
            Field(placeholder: "you@email.com", text: $email, keyboard: .emailAddress)
            Field(placeholder: "Name (optional)", text: $name)
            HStack {
                Button { dismiss() } label: { Caption("Cancel") }
                    .buttonStyle(BlackButton())
                Spacer()
                Button {
                    blog.add(email: email, name: name.isEmpty ? nil : name)
                    dismiss()
                } label: {
                    Caption("Add")
                }
                .buttonStyle(BlackButton())
                .disabled(!isValid)
            }
            Spacer()
        }
        .padding(Theme.pad)
        .font(Theme.body)
        .foregroundStyle(Theme.ink)
        .tint(Theme.ink)
        .background(Theme.paper.ignoresSafeArea())
        .presentationDetents([.medium])
    }
}
