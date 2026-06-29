# Design Patterns — Canonical Taxonomy

The authoritative, language-agnostic list of design patterns in scope across the project
series, organised into eight categories. Pattern names used anywhere in the repository (the
catalogue, ADRs, commit messages, code comments) **must** match the spelling and
categorisation here. When evaluating candidates for a problem, scan the relevant category
first.

This file is reused verbatim across all generated projects; it is a vocabulary, not a
commitment to apply any particular pattern.

## 1. Creational

| Pattern | One-line intent |
|---|---|
| Factory Method | Defer instantiation to a method so subclasses/variants choose the concrete type. |
| Abstract Factory | Create coherent families of related objects without naming concretes. |
| Builder | Construct a complex object step by step with a fluent, validated API. |
| Prototype | Create new objects by cloning a configured exemplar. |
| Object Pool | Reuse a fixed set of pre-allocated objects to avoid churn. |
| Singleton | Guarantee one instance with a global access point (use sparingly). |
| Multiton | A registry of named singletons keyed by a discriminator. |
| Lazy Initialization | Defer creation of an expensive resource until first use. |
| Dependency Injection | Supply collaborators from outside rather than constructing them. |

## 2. Structural

| Pattern | One-line intent |
|---|---|
| Adapter | Convert one interface into another a client expects. |
| Bridge | Decouple an abstraction from its implementation so both vary. |
| Composite | Treat individual objects and compositions uniformly via a tree. |
| Decorator | Attach responsibilities to an object dynamically by wrapping. |
| Facade | Provide a simple entry point over a complex subsystem. |
| Flyweight | Share intrinsic state across many fine-grained objects. |
| Proxy | Stand in for another object to control access (lazy, remote, guard). |
| Private Class Data / Pimpl | Hide implementation state behind an opaque boundary. |

## 3. Behavioral

| Pattern | One-line intent |
|---|---|
| Strategy | Encapsulate interchangeable algorithms behind a common interface. |
| Template Method | Define an algorithm skeleton, deferring steps to hooks. |
| Iterator | Traverse a collection without exposing its representation. |
| State | Alter behavior when internal state changes, as if changing class. |
| Observer | Notify dependents of state changes (publish/subscribe). |
| Memento | Capture and restore an object's state without breaking encapsulation. |
| Null Object | Provide a do-nothing implementation to avoid null checks. |
| Command | Encapsulate a request as an object (queue, log, undo). |
| Chain of Responsibility | Pass a request along a chain until one handler takes it. |
| Mediator | Centralise complex interactions between collaborators. |
| Visitor | Add operations to an object structure without changing it. |
| Interpreter | Define a grammar and an evaluator for a small language. |
| Specification | Compose predicates declaratively to select/validate objects. |

## 4. Enterprise Integration Patterns (EIP)

| Pattern | One-line intent |
|---|---|
| Message Channel | A conduit between producers and consumers. |
| Message Router | Route a message to a destination by its content/headers. |
| Message Translator | Transform a message between formats/schemas. |
| Message Endpoint | Connect an application to the messaging system. |
| Publish-Subscribe | Broadcast an event to all interested subscribers. |
| Competing Consumers | Scale processing across parallel consumers. |
| Dead Letter Channel | Divert undeliverable messages for inspection. |
| Idempotent Receiver | Process duplicate messages safely. |
| Saga / Process Manager | Coordinate a long-running, multi-step transaction. |

## 5. Architectural Application Styles

| Pattern | One-line intent |
|---|---|
| Layered (n-tier) | Separate concerns into stacked layers. |
| Hexagonal (Ports & Adapters) | Isolate the domain behind ports with swappable adapters. |
| Clean / Onion | Dependencies point inward toward the domain. |
| MVC / MVP / MVVM | Separate presentation from model and mediation logic. |
| Microkernel / Plugin | A minimal core extended by plugins. |
| Event-Driven Architecture | Components react to and emit events. |
| CQRS | Separate the write model from the read model. |
| Domain-Driven Design building blocks | Entities, value objects, aggregates, repositories. |

## 6. Concurrency

| Pattern | One-line intent |
|---|---|
| Monitor Object | Serialise access to an object's methods with a lock. |
| Immutable Object | Eliminate races by forbidding post-construction mutation. |
| Guarded Suspension | Block until a precondition holds, then proceed. |
| Active Object | Decouple method invocation from execution via a queue. |
| Thread Pool | Reuse a bounded set of worker threads. |
| Producer-Consumer | Decouple producers and consumers via a buffer. |
| Read-Write Lock | Allow concurrent reads, exclusive writes. |
| Future / Promise | Represent a value that will be available later. |
| Lock-Free / Wait-Free | Coordinate without blocking via atomic operations. |
| Thread-Local Storage | Give each thread its own copy of state. |

## 7. Cloud & Distributed Systems

| Pattern | One-line intent |
|---|---|
| Circuit Breaker | Stop calling a failing dependency to let it recover. |
| Retry with Backoff | Retry transient failures with increasing delay + jitter. |
| Bulkhead | Isolate resources so one failure does not sink the whole. |
| Sidecar / Ambassador | Offload cross-cutting concerns to a co-process. |
| Leader Election | Elect a single coordinator among peers. |
| Sharding / Partitioning | Split data/work across nodes by a key. |
| Saga | Maintain consistency across services without 2PC. |
| Service Discovery | Locate service instances dynamically. |
| Rate Limiting / Throttling | Bound the request rate to protect a resource. |

## 8. Data & Persistence

| Pattern | One-line intent |
|---|---|
| Repository | Mediate between the domain and data mapping layers. |
| Unit of Work | Track changes and commit them as one transaction. |
| Data Mapper | Move data between objects and storage, keeping them independent. |
| Active Record | An object that carries both data and its persistence logic. |
| Identity Map | Ensure each loaded object is loaded only once. |
| Data Access Object (DAO) | Abstract and encapsulate access to a data source. |
| Outbox | Publish events atomically with a database write. |
| Event Sourcing | Persist state as an append-only sequence of events. |
| CQRS (data side) | Separate read and write data models/stores. |
