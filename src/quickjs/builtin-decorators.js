;(function(TypeError, Object·defineProperty, Object·getOwnPropertyDescriptor,
           Object·create, Object·getPrototypeOf, Symbol·metadata, Reflect·apply) {
    "use strict"
    const KINDS = ["class", "method", "getter", "setter", "field", "accessor"]
    const FIELD = 4, ACCESSOR = 5

    function push(list, value) {
        Object·defineProperty(list, list.length, {value, writable: true, enumerable: true, configurable: true})
    }

    function runAll(list, receiver) {
        for (let i = 0; i < list.length; i++) Reflect·apply(list[i], receiver, [])
    }

    function begin(F) {
        return {
            F,
            metadata: undefined,
            slots: [],
            instanceExtra: [],
            staticExtra: [],
            classExtra: [],
        }
    }

    function metadataOf(state) {
        if (state.metadata === undefined) {
            const parent = Object·getPrototypeOf(state.F)
            let parentMetadata = null
            if (parent !== null) {
                const m = parent[Symbol·metadata]
                if (m !== undefined) parentMetadata = m
            }
            state.metadata = Object·create(parentMetadata)
        }
        return state.metadata
    }

    function makeAddInitializer(done, list) {
        return function addInitializer(initializer) {
            if (done.finished) throw new TypeError("addInitializer called after decoration finished")
            if (typeof initializer !== "function") throw new TypeError("initializer must be a function")
            push(list, initializer)
        }
    }

    function accessObject(kind, get, set, has) {
        if (kind === 3) return {set, has}
        if (kind === FIELD || kind === ACCESSOR) return {get, set, has}
        return {get, has}
    }

    function publicAccess(kind, key) {
        return accessObject(kind,
            function get(obj) { return obj[key] },
            function set(obj, value) { obj[key] = value },
            function has(obj) {
                if ((typeof obj !== "object" || obj === null) && typeof obj !== "function")
                    throw new TypeError("access.has expects an object")
                return key in obj
            })
    }

    function define(target, key, desc) {
        Object·defineProperty(target, key, desc)
    }

    function decorateElement(state, target, kind, key, isStatic, isPrivate,
                             decorators, value, get, set, has, slot) {
        const extra = kind === FIELD || kind === ACCESSOR ? [] :
            isStatic ? state.staticExtra : state.instanceExtra
        const inits = []
        let current
        if (isPrivate) {
            current = kind === ACCESSOR ? {get: value[0], set: value[1]} : value
        } else {
            const desc = Object·getOwnPropertyDescriptor(target, key)
            if (kind === 1) current = desc.value
            else if (kind === 2) current = desc.get
            else if (kind === 3) current = desc.set
            else if (kind === ACCESSOR) current = {get: desc.get, set: desc.set}
        }
        const access = isPrivate ? accessObject(kind, get, set, has) : publicAccess(kind, key)
        for (let i = decorators.length - 1; i >= 0; i--) {
            const done = {finished: false}
            const context = {
                kind: KINDS[kind],
                name: key,
                static: isStatic,
                private: isPrivate,
                access,
                addInitializer: makeAddInitializer(done, extra),
                metadata: metadataOf(state),
            }
            let result
            try {
                result = decorators[i](kind === FIELD ? undefined : current, context)
            } finally {
                done.finished = true
            }
            if (result === undefined) continue
            if (kind === FIELD) {
                if (typeof result !== "function") throw new TypeError("field decorators must return a function or undefined")
                push(inits, result)
            } else if (kind === ACCESSOR) {
                if (typeof result !== "object" || result === null)
                    throw new TypeError("accessor decorators must return an object or undefined")
                const { get: newGet, set: newSet, init } = result
                const next = {get: current.get, set: current.set}
                if (newGet !== undefined) {
                    if (typeof newGet !== "function") throw new TypeError("accessor get must be a function")
                    next.get = newGet
                }
                if (newSet !== undefined) {
                    if (typeof newSet !== "function") throw new TypeError("accessor set must be a function")
                    next.set = newSet
                }
                if (init !== undefined) {
                    if (typeof init !== "function") throw new TypeError("accessor init must be a function")
                    push(inits, init)
                }
                current = next
            } else {
                if (typeof result !== "function") throw new TypeError("decorators must return a function or undefined")
                current = result
            }
        }
        Object·defineProperty(state.slots, slot, {value: {inits, extra}, writable: true, enumerable: true, configurable: true})
        if (isPrivate)
            return kind === ACCESSOR ? [current.get, current.set] : current
        if (kind === 1) define(target, key, {value: current})
        else if (kind === 2) define(target, key, {get: current})
        else if (kind === 3) define(target, key, {set: current})
        else if (kind === ACCESSOR) define(target, key, {get: current.get, set: current.set})
        return undefined
    }

    function initField(value, state, slot, obj) {
        const s = state.slots[slot]
        if (s === undefined) return value
        for (let i = 0; i < s.inits.length; i++) value = Reflect·apply(s.inits[i], obj, [value])
        return value
    }

    function afterField(obj, state, slot) {
        const s = state.slots[slot]
        if (s !== undefined) runAll(s.extra, obj)
    }

    function runInstance(obj, state) {
        runAll(state.instanceExtra, obj)
    }

    function runStatic(state, C) {
        runAll(state.staticExtra, C)
    }

    function applyClass(state, C, decorators, name) {
        let current = C
        for (let i = decorators.length - 1; i >= 0; i--) {
            const done = {finished: false}
            const context = {
                kind: "class",
                name,
                addInitializer: makeAddInitializer(done, state.classExtra),
                metadata: metadataOf(state),
            }
            let result
            try {
                result = decorators[i](current, context)
            } finally {
                done.finished = true
            }
            if (result === undefined) continue
            if (typeof result !== "function") throw new TypeError("class decorators must return a function or undefined")
            current = result
        }
        return current
    }

    function setMetadata(state) {
        Object·defineProperty(state.F, Symbol·metadata,
            {value: metadataOf(state), writable: false, enumerable: false, configurable: true})
    }

    function finish(C, state) {
        runAll(state.classExtra, C)
        return C
    }

    return [begin, decorateElement, initField, afterField, runInstance,
            runStatic, applyClass, setMetadata, finish]
})
