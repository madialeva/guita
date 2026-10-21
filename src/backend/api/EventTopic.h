#pragma once
#include <cstdint>
#include <exception>
#include <functional>
#include <iostream>
#include <map>
#include <memory>
#include <mutex>
#include <optional>
#include <utility>

// Server events of one kind: whoever produces them publishes, and every
// subscriber receives each message, in the publisher's thread and in order.
// Subscribers must not block (they only queue the message for their
// transport) and must not unsubscribe from within their call. Thread-safe.
template <class Message>
class EventTopic {
public:
    using Subscriber = std::function<void(const Message &)>;

    // Registers a subscriber, which first receives the greeting, if any, before
    // any published message. Destroying the returned handle unsubscribes: once
    // that returns, the subscriber is not called again. Handles may outlive
    // the topic.
    std::shared_ptr<void> subscribe(Subscriber subscriber, std::optional<Message> greeting = std::nullopt) {
        std::lock_guard lock(m_state->mutex);
        const auto id = m_state->nextId++;
        if (greeting)
            deliver(subscriber, *greeting);
        m_state->subscribers.emplace(id, std::move(subscriber));
        return std::make_shared<Unsubscriber>(m_state, id);
    }

    void publish(const Message &message) {
        std::lock_guard lock(m_state->mutex);
        for (const auto &[id, subscriber] : m_state->subscribers)
            deliver(subscriber, message);
    }

private:
    struct State {
        std::mutex mutex;
        std::map<std::uint64_t, Subscriber> subscribers;
        std::uint64_t nextId = 0;
    };

    struct Unsubscriber {
        Unsubscriber(std::shared_ptr<State> state, std::uint64_t id) : state(std::move(state)), id(id) {}
        ~Unsubscriber() {
            std::lock_guard lock(state->mutex);
            state->subscribers.erase(id);
        }
        std::shared_ptr<State> state;
        std::uint64_t id;
    };

    // A failing subscriber must not stop the others nor the publisher
    static void deliver(const Subscriber &subscriber, const Message &message) {
        try {
            subscriber(message);
        } catch (const std::exception &e) {
            std::cerr << "error: event subscriber: " << e.what() << '\n';
        }
    }

    std::shared_ptr<State> m_state = std::make_shared<State>();
};
