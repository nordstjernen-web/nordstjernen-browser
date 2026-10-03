(function (global) {
    'use strict';

    var ndWorkerScope = !!global.__ndWorkerHeadersOnly;
    if (ndWorkerScope) {
        try { delete global.__ndWorkerHeadersOnly; } catch (e) {}
    }

    function nativeize(fn) {
        return fn;
    }

    if (typeof global.PerformanceObserver === 'function' &&
        !Array.isArray(global.PerformanceObserver.supportedEntryTypes)) {
        try {
            global.PerformanceObserver.supportedEntryTypes = [
                'mark', 'measure', 'navigation', 'resource', 'paint'
            ];
        } catch (e) {}
    }



    function defineCtor(name, ctor) {
        if (typeof global[name] === 'function' || typeof global[name] === 'object'
            && global[name] !== null) return;
        try {
            Object.defineProperty(global, name, {
                value: ctor, writable: true, configurable: true, enumerable: false
            });
        } catch (e) { global[name] = ctor; }
    }

    function replaceCtor(name, ctor) {
        try {
            Object.defineProperty(global, name, {
                value: ctor, writable: true, configurable: true, enumerable: false
            });
        } catch (e) { global[name] = ctor; }
    }

    function defineMethod(proto, name, fn) {
        if (typeof proto[name] === 'function') return;
        try {
            Object.defineProperty(proto, name, {
                value: fn, writable: true, configurable: true, enumerable: false
            });
        } catch (e) { proto[name] = fn; }
    }

    function idlBrand(map) {
        return function (obj) {
            var state = map.get(obj);
            if (state === undefined) throw new TypeError('Illegal invocation');
            return state;
        };
    }

    function idlAsync(self, brand, args, count, iface, member, body) {
        try {
            var state = brand(self);
            idlNeed(args, count, iface, member);
            return Promise.resolve(body(state));
        } catch (e) {
            return Promise.reject(e);
        }
    }

    function idlIllegalConstructor(iface) {
        return new TypeError("Failed to construct '" + iface + "': Illegal constructor");
    }

    function idlNeed(args, count, iface, member) {
        if (args.length >= count) return;
        throw new TypeError("Failed to execute '" + member + "' on '" + iface + "': " +
                            count + (count === 1 ? ' argument' : ' arguments') +
                            ' required, but only ' + args.length + ' present.');
    }

    function idlNeedCtor(args, count, iface) {
        if (args.length >= count) return;
        throw new TypeError("Failed to construct '" + iface + "': " +
                            count + (count === 1 ? ' argument' : ' arguments') +
                            ' required, but only ' + args.length + ' present.');
    }

    function idlExpose(ctor, name, parent) {
        var proto = ctor.prototype;
        Object.getOwnPropertyNames(proto).forEach(function (key) {
            if (key === 'constructor') return;
            var desc = Object.getOwnPropertyDescriptor(proto, key);
            if (desc.enumerable) return;
            desc.enumerable = true;
            Object.defineProperty(proto, key, desc);
        });
        Object.getOwnPropertyNames(ctor).forEach(function (key) {
            if (key === 'length' || key === 'name' || key === 'prototype') return;
            var desc = Object.getOwnPropertyDescriptor(ctor, key);
            if (desc.enumerable) return;
            desc.enumerable = true;
            Object.defineProperty(ctor, key, desc);
        });
        Object.defineProperty(proto, Symbol.toStringTag, { value: name, configurable: true });
        if (parent) {
            Object.setPrototypeOf(proto, parent.prototype);
            Object.setPrototypeOf(ctor, parent);
        }
        replaceCtor(name, ctor);
    }

    function idlConstants(ctor, table) {
        Object.keys(table).forEach(function (key) {
            var desc = { value: table[key], writable: false, enumerable: true, configurable: false };
            Object.defineProperty(ctor, key, desc);
            Object.defineProperty(ctor.prototype, key, desc);
        });
    }

    function idlEventHandlers(proto, types, stateOf) {
        types.forEach(function (type) {
            var name = 'on' + type;
            var getName = 'get ' + name;
            var setName = 'set ' + name;
            var accessors = {
                [getName]() { return stateOf(this).handlers[type] || null; },
                [setName](value) {
                    stateOf(this).handlers[type] = typeof value === 'function' ? value : null;
                }
            };
            Object.defineProperty(proto, name, {
                get: accessors[getName], set: accessors[setName],
                enumerable: true, configurable: true
            });
        });
    }

    function idlSingletonBrand(proto, state) {
        var isPrototypeOf = Object.prototype.isPrototypeOf;
        return function (obj) {
            if (!isPrototypeOf.call(proto, obj)) throw new TypeError('Illegal invocation');
            return state;
        };
    }

    function idlHandlerState(state) {
        state.handlers = Object.create(null);
        return state;
    }

    function idlTrustedEvent(event) {
        event._is_trusted = true;
        return event;
    }

    function idlFireEvent(target, type) {
        return target.dispatchEvent(idlTrustedEvent(new Event(type)));
    }

    function idlEventTarget() {
        return typeof global.EventTarget === 'function' ? global.EventTarget : null;
    }

    function idlPinTarget(event, target) {
        Object.defineProperty(event, 'target', {
            get: function () { return target; },
            set: function () {},
            configurable: true
        });
    }

    function idlDispatchPath(event, path) {
        if (typeof __ndDispatchPath === 'function')
            return __ndDispatchPath(event, path);
        for (var i = 0; i < path.length; i++) {
            path[i].dispatchEvent(event);
            if (event.cancelBubble) break;
        }
        return !event.defaultPrevented;
    }

    var domStringLists = new WeakMap();
    var domStringList = idlBrand(domStringLists);

    class DOMStringList {
        constructor() { throw idlIllegalConstructor('DOMStringList'); }
        get length() { return domStringList(this).length; }
        item(index) {
            var items = domStringList(this);
            idlNeed(arguments, 1, 'DOMStringList', 'item');
            index = index >>> 0;
            return index < items.length ? items[index] : null;
        }
        contains(string) {
            var items = domStringList(this);
            idlNeed(arguments, 1, 'DOMStringList', 'contains');
            return items.indexOf(String(string)) >= 0;
        }
    }
    Object.defineProperty(DOMStringList.prototype, Symbol.iterator,
        { value: Array.prototype[Symbol.iterator], writable: true, configurable: true });
    idlExpose(DOMStringList, 'DOMStringList', null);

    function newDOMStringList(items) {
        var list = Object.create(DOMStringList.prototype);
        var copy = items.slice();
        domStringLists.set(list, copy);
        for (var i = 0; i < copy.length; i++)
            Object.defineProperty(list, i, { value: copy[i], enumerable: true, configurable: true });
        return list;
    }

    function encodeKV(s) {
        return encodeURIComponent(String(s == null ? '' : s)).replace(/%20/g, '+');
    }
    function decodeKV(s) {
        return decodeURIComponent(String(s == null ? '' : s).replace(/\+/g, ' '));
    }

    function USP(init) {
        if (!(this instanceof USP)) return new USP(init);
        this._p = [];
        if (init == null) return;
        if (init instanceof USP) {
            for (var i = 0; i < init._p.length; i++)
                this._p.push([init._p[i][0], init._p[i][1]]);
            return;
        }
        if (typeof init === 'string') {
            var s = init.charAt(0) === '?' ? init.slice(1) : init;
            if (!s) return;
            var parts = s.split('&');
            for (var j = 0; j < parts.length; j++) {
                if (!parts[j]) continue;
                var eq = parts[j].indexOf('=');
                if (eq < 0) this._p.push([decodeKV(parts[j]), '']);
                else this._p.push([decodeKV(parts[j].slice(0, eq)),
                                   decodeKV(parts[j].slice(eq + 1))]);
            }
            return;
        }
        if (typeof init === 'object') {
            if (typeof init.length === 'number' &&
                typeof init !== 'function') {
                for (var k = 0; k < init.length; k++) {
                    var pair = init[k];
                    if (pair && typeof pair.length === 'number' && pair.length >= 2)
                        this._p.push([String(pair[0]), String(pair[1])]);
                }
                return;
            }
            var keys = Object.keys(init);
            for (var n = 0; n < keys.length; n++)
                this._p.push([keys[n], String(init[keys[n]])]);
        }
    }
    USP.prototype.append = function (k, v) { this._p.push([String(k), String(v)]); };
    USP.prototype.delete = function (k) {
        k = String(k);
        this._p = this._p.filter(function (p) { return p[0] !== k; });
    };
    USP.prototype.get = function (k) {
        k = String(k);
        for (var i = 0; i < this._p.length; i++)
            if (this._p[i][0] === k) return this._p[i][1];
        return null;
    };
    USP.prototype.getAll = function (k) {
        k = String(k);
        var out = [];
        for (var i = 0; i < this._p.length; i++)
            if (this._p[i][0] === k) out.push(this._p[i][1]);
        return out;
    };
    USP.prototype.has = function (k) { return this.get(k) !== null; };
    USP.prototype.set = function (k, v) {
        k = String(k); v = String(v);
        var found = false, out = [];
        for (var i = 0; i < this._p.length; i++) {
            if (this._p[i][0] === k) {
                if (!found) { out.push([k, v]); found = true; }
            } else out.push(this._p[i]);
        }
        if (!found) out.push([k, v]);
        this._p = out;
    };
    USP.prototype.sort = function () {
        this._p.sort(function (a, b) {
            return a[0] < b[0] ? -1 : a[0] > b[0] ? 1 : 0;
        });
    };
    USP.prototype.toString = function () {
        var parts = [];
        for (var i = 0; i < this._p.length; i++)
            parts.push(encodeKV(this._p[i][0]) + '=' + encodeKV(this._p[i][1]));
        return parts.join('&');
    };
    USP.prototype.forEach = function (fn, thisArg) {
        for (var i = 0; i < this._p.length; i++)
            fn.call(thisArg, this._p[i][1], this._p[i][0], this);
    };
    USP.prototype.keys = function () {
        var arr = this._p.map(function (p) { return p[0]; });
        return arr[Symbol.iterator]();
    };
    USP.prototype.values = function () {
        var arr = this._p.map(function (p) { return p[1]; });
        return arr[Symbol.iterator]();
    };
    USP.prototype.entries = function () {
        var arr = this._p.map(function (p) { return [p[0], p[1]]; });
        return arr[Symbol.iterator]();
    };
    if (typeof Symbol !== 'undefined' && Symbol.iterator) {
        USP.prototype[Symbol.iterator] = USP.prototype.entries;
    }
    Object.defineProperty(USP.prototype, 'size', {
        get: function () { return this._p.length; },
        configurable: true
    });
    defineCtor('URLSearchParams', USP);

    function normHeader(k) { return String(k).toLowerCase(); }
    var HTTP_WS = /^[\t\n\r ]+|[\t\n\r ]+$/g;
    var HDR_TOKEN = /^[!#$%&'*+\-.^_`|~0-9A-Za-z]+$/;
    var HDR_BADVAL = /[\0\n\r]/;
    function checkHeaderName(k) {
        var s = String(k);
        if (!HDR_TOKEN.test(s))
            throw new TypeError("Invalid header name: '" + s + "'");
        return s.toLowerCase();
    }
    function checkHeaderValue(v) {
        var s = String(v).replace(HTTP_WS, '');
        if (HDR_BADVAL.test(s))
            throw new TypeError("Invalid header value");
        return s;
    }
    function hdrByteString(x) {
        var s = String(x);
        for (var i = 0; i < s.length; i++)
            if (s.charCodeAt(i) > 0xFF)
                throw new TypeError("Header contains a character outside the ByteString range");
        return s;
    }
    var hdrIterState = new WeakMap();
    var HDR_ITER_PROTO = Object.create(
        Object.getPrototypeOf(Object.getPrototypeOf([][Symbol.iterator]())));
    Object.defineProperty(HDR_ITER_PROTO, 'next', {
        configurable: true, enumerable: true, writable: true,
        value: function () {
            var st = hdrIterState.get(this);
            if (!st) throw new TypeError('Illegal invocation');
            var entries = headersEntries(st.h);
            if (st.i >= entries.length) return { value: undefined, done: true };
            var e = entries[st.i++];
            var out = st.k === 0 ? e[0] : st.k === 1 ? e[1] : [e[0], e[1]];
            return { value: out, done: false };
        }
    });
    Object.defineProperty(HDR_ITER_PROTO, Symbol.toStringTag, {
        value: 'Headers Iterator', configurable: true
    });
    function headersIterator(h, kind) {
        var it = Object.create(HDR_ITER_PROTO);
        hdrIterState.set(it, { h: h, i: 0, k: kind });
        return it;
    }
    function headersMap(h) {
        var m = h !== null && typeof h === 'object' ? h.__ndHeaderMap : undefined;
        if (m === undefined) throw new TypeError('Illegal invocation');
        return m;
    }
    function headersEntries(h) {
        var m = headersMap(h), keys = Object.keys(m).sort(), out = [];
        for (var i = 0; i < keys.length; i++) {
            if (keys[i] === 'set-cookie') {
                var cookies = h.__ndSetCookies;
                for (var c = 0; c < cookies.length; c++) out.push([keys[i], cookies[c]]);
            } else out.push([keys[i], m[keys[i]]]);
        }
        return out;
    }

    function Headers(init) {
        if (!(this instanceof Headers))
            throw new TypeError("Constructor Headers requires 'new'");
        this.__ndHeaderMap = Object.create(null);
        this.__ndSetCookies = [];
        if (init === undefined) return;
        if (init === null || (typeof init !== 'object' && typeof init !== 'function'))
            throw new TypeError("Failed to construct 'Headers': invalid init");
        var self = this;
        if (typeof init[Symbol.iterator] !== 'undefined') {
            if (typeof init[Symbol.iterator] !== 'function')
                throw new TypeError("Headers init is not iterable");
            var it = init[Symbol.iterator](), step;
            while (!(step = it.next()).done) {
                var pair = step.value, arr = [];
                if (pair == null || typeof pair[Symbol.iterator] !== 'function')
                    throw new TypeError("Header pair is not iterable");
                var pit = pair[Symbol.iterator](), ps;
                while (!(ps = pit.next()).done) arr.push(ps.value);
                if (arr.length !== 2)
                    throw new TypeError("Header pair must contain exactly two items");
                self.append(arr[0], arr[1]);
            }
            return;
        }
        var keys = Reflect.ownKeys(init);
        var rec = [];
        for (var j = 0; j < keys.length; j++) {
            var key = keys[j];
            var d = Reflect.getOwnPropertyDescriptor(init, key);
            if (d === undefined || !d.enumerable) continue;
            if (typeof key === 'symbol')
                throw new TypeError("Header name cannot be a Symbol");
            var nm = hdrByteString(key);
            rec.push([nm, hdrByteString(init[key])]);
        }
        for (var r = 0; r < rec.length; r++) self.append(rec[r][0], rec[r][1]);
    }
    Headers.prototype.append = function (k, v) {
        var m = headersMap(this);
        var key = checkHeaderName(k);
        var val = checkHeaderValue(v);
        if (m[key] != null) m[key] += ', ' + val;
        else m[key] = val;
        if (key === 'set-cookie') this.__ndSetCookies.push(val);
    };
    Headers.prototype.set = function (k, v) {
        var m = headersMap(this);
        var key = checkHeaderName(k);
        var val = checkHeaderValue(v);
        m[key] = val;
        if (key === 'set-cookie') this.__ndSetCookies = [val];
    };
    Headers.prototype.get = function (k) {
        var m = headersMap(this);
        var v = m[checkHeaderName(k)];
        return v == null ? null : v;
    };
    Headers.prototype.has = function (k) {
        var m = headersMap(this);
        return m[checkHeaderName(k)] != null;
    };
    Headers.prototype.delete = function (k) {
        var m = headersMap(this);
        var key = checkHeaderName(k);
        delete m[key];
        if (key === 'set-cookie') this.__ndSetCookies = [];
    };
    Headers.prototype.forEach = function (fn) {
        var thisArg = arguments[1];
        headersMap(this);
        if (typeof fn !== 'function')
            throw new TypeError("Failed to execute 'forEach' on 'Headers': parameter 1 is not of type 'Function'.");
        var entries = headersEntries(this);
        for (var i = 0; i < entries.length; i++)
            fn.call(thisArg, entries[i][1], entries[i][0], this);
    };
    Headers.prototype.getSetCookie = function () {
        headersMap(this);
        return this.__ndSetCookies.slice();
    };
    Headers.prototype.keys = function () { headersMap(this); return headersIterator(this, 0); };
    Headers.prototype.values = function () { headersMap(this); return headersIterator(this, 1); };
    Headers.prototype.entries = function () { headersMap(this); return headersIterator(this, 2); };
    if (typeof Symbol !== 'undefined' && Symbol.iterator) {
        Object.defineProperty(Headers.prototype, Symbol.iterator, {
            value: Headers.prototype.entries, writable: true, configurable: true
        });
    }
    Object.defineProperty(Headers.prototype, Symbol.toStringTag, {
        value: 'Headers', configurable: true
    });
    nativeize(Headers, 'Headers');
    try { Object.defineProperty(Headers, 'length', { value: 0 }); } catch (e) {}
    defineCtor('Headers', Headers);

    function utf8Encode(s) {
        var str = String(s);
        var out = [];
        for (var i = 0; i < str.length; i++) {
            var c = str.charCodeAt(i);
            if (c < 0x80) {
                out.push(c);
            } else if (c < 0x800) {
                out.push(0xc0 | (c >> 6), 0x80 | (c & 0x3f));
            } else if (c >= 0xd800 && c <= 0xdfff) {
                var c2 = i + 1 < str.length ? str.charCodeAt(i + 1) : 0;
                if (c <= 0xdbff && c2 >= 0xdc00 && c2 <= 0xdfff) {
                    var cp = 0x10000 + ((c - 0xd800) << 10) + (c2 - 0xdc00);
                    out.push(0xf0 | (cp >> 18),
                             0x80 | ((cp >> 12) & 0x3f),
                             0x80 | ((cp >> 6)  & 0x3f),
                             0x80 | (cp & 0x3f));
                    i++;
                } else {
                    out.push(0xef, 0xbf, 0xbd);
                }
            } else {
                out.push(0xe0 | (c >> 12),
                         0x80 | ((c >> 6) & 0x3f),
                         0x80 | (c & 0x3f));
            }
        }
        return new Uint8Array(out);
    }

    function blobIdlString(v) {
        if (typeof v === 'symbol')
            throw new TypeError('Cannot convert a Symbol value to a string');
        return String(v);
    }

    function blobNativeEndings(s) {
        var platform = typeof navigator !== 'undefined' && navigator ?
            String(navigator.platform || '') : '';
        var eol = platform.indexOf('Win') === 0 ? '\r\n' : '\n';
        return s.replace(/\r\n|\r|\n/g, eol);
    }

    function blobBufferBytes(buffer, offset, length) {
        if (buffer.detached) return new Uint8Array(0);
        return new Uint8Array(buffer.slice(offset, offset + length));
    }

    function blobPartItem(part) {
        if (part instanceof ArrayBuffer)
            return blobBufferBytes(part, 0, part.byteLength);
        if (ArrayBuffer.isView(part))
            return blobBufferBytes(part.buffer, part.byteOffset, part.byteLength);
        if (part instanceof Blob) return part.__ndBlobBytes || new Uint8Array(0);
        return blobIdlString(part);
    }

    function blobItemBytes(item, endings) {
        if (typeof item !== 'string') return item;
        if (endings === 'native') item = blobNativeEndings(item);
        if (typeof TextEncoder === 'function') return new TextEncoder().encode(item);
        return utf8Encode(item);
    }

    function blobNormalizeType(t) {
        for (var i = 0; i < t.length; i++) {
            var c = t.charCodeAt(i);
            if (c < 0x20 || c > 0x7e) return '';
        }
        return t.toLowerCase();
    }

    function blobPropertyBag(options, withLastModified) {
        var bag = { endings: 'transparent', type: '', lastModified: undefined };
        if (options === undefined || options === null) return bag;
        if (typeof options !== 'object' && typeof options !== 'function')
            throw new TypeError('The options argument is not an object');
        var endings = options.endings;
        if (endings !== undefined) {
            endings = blobIdlString(endings);
            if (endings !== 'transparent' && endings !== 'native')
                throw new TypeError('The endings option must be "transparent" or "native"');
            bag.endings = endings;
        }
        if (withLastModified) {
            var lm = options.lastModified;
            if (lm !== undefined) bag.lastModified = blobLongLong(lm, false);
        }
        var type = options.type;
        if (type !== undefined) bag.type = blobIdlString(type);
        return bag;
    }

    function blobLongLong(v, clamp) {
        var n = +v;
        if (!isFinite(n)) {
            if (!clamp || isNaN(n)) return 0;
            return n > 0 ? Number.MAX_SAFE_INTEGER : Number.MIN_SAFE_INTEGER;
        }
        if (clamp) {
            n = Math.min(Math.max(n, Number.MIN_SAFE_INTEGER), Number.MAX_SAFE_INTEGER);
            var f = Math.floor(n);
            var d = n - f;
            if (d > 0.5 || (d === 0.5 && f % 2 !== 0)) f += 1;
            return f === 0 ? 0 : f;
        }
        n = Math.trunc(n);
        return n === 0 ? 0 : n;
    }

    function blobCollectParts(parts) {
        var items = [];
        if (parts === undefined) return items;
        if (parts === null || (typeof parts !== 'object' && typeof parts !== 'function'))
            throw new TypeError('The blobParts argument is not a sequence');
        var method = parts[Symbol.iterator];
        if (typeof method !== 'function')
            throw new TypeError('The blobParts argument is not iterable');
        var iterator = method.call(parts);
        if (iterator === null || typeof iterator !== 'object')
            throw new TypeError('The blobParts iterator is not an object');
        var next = iterator.next;
        for (;;) {
            var step = next.call(iterator);
            if (step === null || typeof step !== 'object')
                throw new TypeError('The blobParts iterator result is not an object');
            if (step.done) break;
            items.push(blobPartItem(step.value));
        }
        return items;
    }

    function blobConcatParts(items, endings) {
        var chunks = [];
        var total = 0;
        for (var i = 0; i < items.length; i++) {
            var b = blobItemBytes(items[i], endings);
            chunks.push(b);
            total += b.length;
        }
        var buf = new Uint8Array(total);
        var off = 0;
        for (var k = 0; k < chunks.length; k++) {
            buf.set(chunks[k], off);
            off += chunks[k].length;
        }
        return buf;
    }

    function blobInit(self, bytes, type) {
        self.__ndBlobBytes = bytes;
        self.__ndBlobType = type;
    }

    function Blob() {
        if (!new.target) throw new TypeError("Failed to construct 'Blob': Please use the 'new' operator");
        var items = blobCollectParts(arguments[0]);
        var bag = blobPropertyBag(arguments[1], false);
        blobInit(this, blobConcatParts(items, bag.endings),
                 blobNormalizeType(bag.type));
    }
    Object.defineProperty(Blob, 'length', { value: 0 });
    function blobDefine(proto, name, fn) {
        Object.defineProperty(proto, name, {
            value: fn, writable: true, configurable: true, enumerable: true
        });
    }
    function blobGetter(proto, name, fn) {
        Object.defineProperty(proto, name, {
            get: fn, configurable: true, enumerable: true
        });
    }
    function blobBytesOf(blob) {
        var bytes = blob !== null && typeof blob === 'object' ? blob.__ndBlobBytes : undefined;
        if (!ArrayBuffer.isView(bytes)) throw new TypeError('Illegal invocation');
        return bytes;
    }
    function blobAsync(fn) {
        return function () {
            try { return fn.call(this); } catch (e) { return Promise.reject(e); }
        };
    }
    blobGetter(Blob.prototype, 'size', function () { return blobBytesOf(this).length; });
    blobGetter(Blob.prototype, 'type', function () {
        blobBytesOf(this);
        return this.__ndBlobType;
    });
    blobDefine(Blob.prototype, 'slice', function () {
        var start = arguments[0], end = arguments[1], contentType = arguments[2];
        var bytes = blobBytesOf(this);
        var size = bytes.length;
        var from = start === undefined ? 0 : blobLongLong(start, true);
        var to = end === undefined ? size : blobLongLong(end, true);
        from = from < 0 ? Math.max(size + from, 0) : Math.min(from, size);
        to = to < 0 ? Math.max(size + to, 0) : Math.min(to, size);
        var type = contentType === undefined ? '' : blobNormalizeType(blobIdlString(contentType));
        var out = Object.create(Blob.prototype);
        blobInit(out, bytes.slice(from, Math.max(to, from)), type);
        return out;
    });
    function utf8Decode(bytes) {
        var s = '';
        for (var i = 0; i < bytes.length;) {
            var b1 = bytes[i++];
            if (b1 < 0x80) {
                s += String.fromCharCode(b1);
            } else if (b1 < 0xc0) {
                s += '�';
            } else if (b1 < 0xe0) {
                var b2 = bytes[i++] & 0x3f;
                s += String.fromCharCode(((b1 & 0x1f) << 6) | b2);
            } else if (b1 < 0xf0) {
                var c2 = bytes[i++] & 0x3f;
                var c3 = bytes[i++] & 0x3f;
                s += String.fromCharCode(((b1 & 0x0f) << 12) | (c2 << 6) | c3);
            } else {
                var d2 = bytes[i++] & 0x3f;
                var d3 = bytes[i++] & 0x3f;
                var d4 = bytes[i++] & 0x3f;
                var cp = ((b1 & 0x07) << 18) | (d2 << 12) | (d3 << 6) | d4;
                cp -= 0x10000;
                s += String.fromCharCode(0xd800 | (cp >> 10),
                                         0xdc00 | (cp & 0x3ff));
            }
        }
        return s;
    }
    blobDefine(Blob.prototype, 'text', blobAsync(function () {
        var b = blobBytesOf(this);
        var text = (typeof TextDecoder === 'function')
            ? new TextDecoder().decode(b) : utf8Decode(b);
        return Promise.resolve(text);
    }));
    blobDefine(Blob.prototype, 'arrayBuffer', blobAsync(function () {
        var b = blobBytesOf(this);
        var buf = new ArrayBuffer(b.length);
        new Uint8Array(buf).set(b);
        return Promise.resolve(buf);
    }));
    blobDefine(Blob.prototype, 'bytes', blobAsync(function () {
        return Promise.resolve(new Uint8Array(blobBytesOf(this)));
    }));
    blobDefine(Blob.prototype, 'stream', function () {
        var bytes = blobBytesOf(this);
        if (typeof ReadableStream === 'function') {
            return new ReadableStream({
                start: function (controller) {
                    if (bytes && bytes.length)
                        controller.enqueue(new Uint8Array(bytes));
                    controller.close();
                }
            });
        }
        var done = false;
        return {
            getReader: function () {
                return {
                    read: function () {
                        if (done)
                            return Promise.resolve({ done: true, value: undefined });
                        done = true;
                        return Promise.resolve({ done: false, value: new Uint8Array(bytes) });
                    },
                    releaseLock: function () {},
                    cancel: function () { done = true; return Promise.resolve(); }
                };
            }
        };
    });
    Object.defineProperty(Blob.prototype, Symbol.toStringTag, {
        value: 'Blob', configurable: true
    });
    defineCtor('Blob', Blob);

    function File(fileBits, fileName) {
        if (!new.target) throw new TypeError("Failed to construct 'File': Please use the 'new' operator");
        if (arguments.length < 2)
            throw new TypeError("Failed to construct 'File': 2 arguments required");
        if (fileBits === undefined)
            throw new TypeError("Failed to construct 'File': fileBits is not a sequence");
        var items = blobCollectParts(fileBits);
        var name = blobIdlString(fileName);
        var bag = blobPropertyBag(arguments[2], true);
        blobInit(this, blobConcatParts(items, bag.endings),
                 blobNormalizeType(bag.type));
        this.__ndFileName = name;
        this.__ndFileMtime = bag.lastModified === undefined ? Date.now() : bag.lastModified;
    }
    Object.defineProperty(File, 'length', { value: 2 });
    File.prototype = Object.create(Blob.prototype);
    Object.defineProperty(File.prototype, 'constructor', {
        value: File, writable: true, configurable: true
    });
    Object.setPrototypeOf(File, Blob);
    function fileBrand(file) {
        blobBytesOf(file);
        if (typeof file.__ndFileName !== 'string') throw new TypeError('Illegal invocation');
        return file;
    }
    blobGetter(File.prototype, 'name', function () { return fileBrand(this).__ndFileName; });
    blobGetter(File.prototype, 'lastModified', function () { return fileBrand(this).__ndFileMtime; });
    blobGetter(File.prototype, 'webkitRelativePath', function () { fileBrand(this); return ''; });
    Object.defineProperty(File.prototype, Symbol.toStringTag, {
        value: 'File', configurable: true
    });
    defineCtor('File', File);

    if (typeof global.queueMicrotask !== 'function') {
        defineCtor('queueMicrotask', function (cb) {
            Promise.resolve().then(cb);
        });
    }

    function ndMediaTask(fn) {
        if (typeof queueMicrotask === 'function') queueMicrotask(fn);
        else setTimeout(fn, 0);
    }

    function ndDomError(name, message) {
        if (typeof global.DOMException === 'function') {
            try { return new global.DOMException(message || name, name); }
            catch (e) {}
        }
        var error = new Error(message || name);
        error.name = name;
        return error;
    }

    function ndMediaEvent(type, target) {
        var ev;
        try { ev = new Event(type); } catch (e) { ev = { type: String(type) }; }
        try {
            Object.defineProperty(ev, 'target', { configurable: true, value: target });
            Object.defineProperty(ev, 'currentTarget', { configurable: true, value: target });
        } catch (e) {
            ev.target = target;
            ev.currentTarget = target;
        }
        return ev;
    }

    function ndEventMethods(proto) {
        proto.addEventListener = function (type, cb) {
            if (!cb) return;
            type = String(type);
            if (!this._listeners) this._listeners = {};
            if (!this._listeners[type]) this._listeners[type] = [];
            if (this._listeners[type].indexOf(cb) < 0)
                this._listeners[type].push(cb);
        };
        proto.removeEventListener = function (type, cb) {
            type = String(type);
            var list = this._listeners && this._listeners[type];
            if (!list) return;
            var i = list.indexOf(cb);
            if (i >= 0) list.splice(i, 1);
        };
        proto.dispatchEvent = function (ev) {
            if (!ev || !ev.type) return true;
            return ndFireEvent(this, ev.type, ev);
        };
    }

    function ndAccessors(proto, getters) {
        Object.keys(getters).forEach(function (name) {
            Object.defineProperty(proto, name, {
                configurable: true,
                enumerable: true,
                get: getters[name]
            });
        });
    }

    function ndFireEvent(target, type, ev) {
        ev = ev || ndMediaEvent(type, target);
        var handler = target && target['on' + type];
        if (typeof handler === 'function') {
            try { handler.call(target, ev); } catch (e) {}
        }
        var list = target && target._listeners && target._listeners[type];
        if (list) {
            list = list.slice();
            for (var i = 0; i < list.length; i++) {
                try {
                    if (typeof list[i] === 'function') list[i].call(target, ev);
                    else if (list[i] && typeof list[i].handleEvent === 'function')
                        list[i].handleEvent(ev);
                } catch (e) {}
            }
        }
        return true;
    }

    function ndTimeRanges(start, end) {
        this.length = end > start ? 1 : 0;
        this._start = start || 0;
        this._end = end || 0;
    }
    ndTimeRanges.prototype.start = function (index) {
        if (index !== 0 || this.length === 0)
            throw ndDomError('IndexSizeError', 'TimeRanges index is out of bounds');
        return this._start;
    };
    ndTimeRanges.prototype.end = function (index) {
        if (index !== 0 || this.length === 0)
            throw ndDomError('IndexSizeError', 'TimeRanges index is out of bounds');
        return this._end;
    };
    if (typeof global.TimeRanges !== 'function') {
        var TimeRanges = function () {
            throw new TypeError('Illegal constructor');
        };
        try {
            Object.defineProperty(TimeRanges.prototype, Symbol.toStringTag,
                                  { configurable: true, value: 'TimeRanges' });
        } catch (e) {}
        global.TimeRanges = TimeRanges;
    }
    if (typeof global.TimeRanges === 'function' && global.TimeRanges.prototype) {
        try {
            Object.setPrototypeOf(ndTimeRanges.prototype,
                                  global.TimeRanges.prototype);
        } catch (e) {}
    }

    function ndTrackList() {}
    ndTrackList.prototype.item = function (index) {
        return this[index >>> 0] || null;
    };
    ndTrackList.prototype.getTrackById = function (id) {
        id = String(id);
        for (var i = 0; i < this.length; i++)
            if (this[i] && String(this[i].id) === id) return this[i];
        return null;
    };
    if (idlEventTarget()) Object.setPrototypeOf(ndTrackList.prototype, idlEventTarget().prototype);
    if (typeof global.TextTrackList !== 'function') {
        var textTrackList = function () { throw new TypeError('Illegal constructor'); };
        textTrackList.prototype = Object.create(ndTrackList.prototype);
        try {
            Object.defineProperty(textTrackList.prototype, Symbol.toStringTag,
                                  { configurable: true, value: 'TextTrackList' });
        } catch (e) {}
        global.TextTrackList = textTrackList;
    }

    var mediaSources = new WeakMap();
    var sourceBuffers = new WeakMap();
    var sourceBufferLists = new WeakMap();
    var mediaSourceHandles = new WeakMap();
    var mediaSourceOf = idlBrand(mediaSources);
    var sourceBufferOf = idlBrand(sourceBuffers);
    var sourceBufferListOf = idlBrand(sourceBufferLists);
    var mediaSourceHandleOf = idlBrand(mediaSourceHandles);

    class SourceBufferList {
        constructor() { throw idlIllegalConstructor('SourceBufferList'); }
        get length() { return sourceBufferListOf(this).items.length; }
    }
    Object.defineProperty(SourceBufferList.prototype, Symbol.iterator,
        { value: Array.prototype[Symbol.iterator], writable: true, configurable: true });
    idlEventHandlers(SourceBufferList.prototype, ['addsourcebuffer', 'removesourcebuffer'],
                     sourceBufferListOf);

    function newSourceBufferList() {
        var list = Object.create(SourceBufferList.prototype);
        sourceBufferLists.set(list, idlHandlerState({ items: [], indexed: 0 }));
        return list;
    }

    function syncSourceBufferList(list) {
        var s = sourceBufferLists.get(list);
        for (var i = s.items.length; i < s.indexed; i++) delete list[i];
        for (var j = 0; j < s.items.length; j++)
            Object.defineProperty(list, j, { value: s.items[j], enumerable: true, configurable: true });
        s.indexed = s.items.length;
    }

    function pushSourceBuffer(list, buffer) {
        sourceBufferLists.get(list).items.push(buffer);
        syncSourceBufferList(list);
        idlFireEvent(list, 'addsourcebuffer');
    }

    function dropSourceBuffer(list, buffer) {
        var items = sourceBufferLists.get(list).items;
        var i = items.indexOf(buffer);
        if (i < 0) return false;
        items.splice(i, 1);
        syncSourceBufferList(list);
        idlFireEvent(list, 'removesourcebuffer');
        return true;
    }

    class MediaSourceHandle {
        constructor() { throw idlIllegalConstructor('MediaSourceHandle'); }
    }

    var ndTypeSupportCache = Object.create(null);

    function ndProbeMediaType(raw, mime) {
        if (typeof global.__ndMseTypeSupported === 'function')
            return !!global.__ndMseTypeSupported(raw);
        var probe = global.document && global.document.createElement &&
            global.document.createElement(mime.indexOf('audio/') === 0 ? 'audio' : 'video');
        if (probe && typeof probe.canPlayType === 'function' &&
            probe.canPlayType(raw))
            return true;
        return mime === 'audio/mpeg' || mime === 'audio/mp3' ||
               mime === 'video/mpeg';
    }

    function ndSupportedMediaType(type) {
        var raw = String(type || '').toLowerCase().trim();
        var mime = raw.split(';')[0].trim();
        if (!mime) return false;
        var hit = ndTypeSupportCache[raw];
        if (hit === undefined)
            hit = ndTypeSupportCache[raw] = ndProbeMediaType(raw, mime);
        return hit;
    }

    function ndRetargetMediaSourceUrl(from, to, audioUrl) {
        if (!from || !to || from === to ||
            !global.document || !global.document.querySelectorAll)
            return;
        var nodes;
        try { nodes = global.document.querySelectorAll('video,audio,source'); }
        catch (e) { nodes = []; }
        for (var i = 0; i < nodes.length; i++) {
            var node = nodes[i];
            var attr = '';
            try { attr = node.getAttribute && node.getAttribute('src'); } catch (e) {}
            var prop = '';
            try { prop = node.src || ''; } catch (e) {}
            if (attr === from || prop === from) {
                try {
                    if (audioUrl && node.setAttribute)
                        node.setAttribute('data-audio-src', audioUrl);
                    if (node.setAttribute) node.setAttribute('src', to);
                    else node.src = to;
                    if (typeof node.load === 'function') node.load();
                } catch (e) {}
            }
        }
    }

    var ndMseNative = typeof global.__ndMseAppend === 'function' &&
                      typeof global.__ndMseEos === 'function';
    var ndMseNextId = 0;
    var ndSourceBufferQuota = 256 * 1024 * 1024;

    function ndBufferSourceBytes(data) {
        if (ArrayBuffer.isView(data))
            return blobBufferBytes(data.buffer, data.byteOffset, data.byteLength);
        if (data instanceof ArrayBuffer)
            return blobBufferBytes(data, 0, data.byteLength);
        throw new TypeError("Failed to execute 'appendBuffer' on 'SourceBuffer': " +
                            "parameter 1 is not of type '(ArrayBuffer or ArrayBufferView)'");
    }

    function mediaSourceMedia(url) {
        if (!url || !global.document || !global.document.querySelectorAll) return [];
        var nodes;
        try { nodes = global.document.querySelectorAll('video,audio'); }
        catch (e) { nodes = []; }
        var out = [];
        for (var i = 0; i < nodes.length; i++) {
            var el = nodes[i];
            var src = '';
            try { src = el.src || el.getAttribute('src') || ''; }
            catch (e) {}
            if (src === url) out.push(el);
        }
        return out;
    }

    function mediaSourceSetDuration(ms, value) {
        var s = mediaSources.get(ms);
        s.duration = value;
        var media = mediaSourceMedia(s.url);
        for (var i = 0; i < media.length; i++) {
            var el = media[i];
            try {
                var prev = el._nd_duration;
                if (!(prev > value)) {
                    el._nd_duration = value;
                    if (typeof el.dispatchEvent === 'function' &&
                        typeof Event === 'function')
                        el.dispatchEvent(new Event('durationchange'));
                }
            } catch (e) {}
        }
    }

    function mediaSourceDecodeError(ms) {
        var s = mediaSources.get(ms);
        if (s.readyState !== 'open') return;
        s.readyState = 'ended';
        idlFireEvent(ms, 'sourceended');
        var media = mediaSourceMedia(s.url);
        for (var i = 0; i < media.length; i++) {
            try { idlFireEvent(media[i], 'error'); } catch (e) {}
        }
    }

    function mediaSourceOpen(ms) {
        var s = mediaSources.get(ms);
        if (s.readyState !== 'closed') return;
        s.readyState = 'open';
        idlFireEvent(ms, 'sourceopen');
    }

    function mediaSourceReopen(ms) {
        var s = mediaSources.get(ms);
        if (s.readyState !== 'ended') return;
        s.readyState = 'open';
        ndMediaTask(function () { idlFireEvent(ms, 'sourceopen'); });
    }

    function mediaSourceRefreshBlob(ms) {
        var s = mediaSources.get(ms);
        if (!s.url || typeof Blob !== 'function' ||
            typeof global.__ndUpdateBlobURL !== 'function')
            return false;
        var items = sourceBufferLists.get(s.sourceBuffers).items;
        var parts = [];
        var type = '';
        var bytes = 0;
        var selected = null;
        for (var i = 0; i < items.length; i++) {
            var buffer = items[i];
            if (!buffer) continue;
            if (!selected || sourceBuffers.get(buffer).type.indexOf('video/') === 0)
                selected = buffer;
            if (selected && sourceBuffers.get(selected).type.indexOf('video/') === 0)
                break;
        }
        if (selected) {
            var ss = sourceBuffers.get(selected);
            type = ss.type || '';
            bytes = ss.bytes || 0;
            for (var j = 0; j < ss.parts.length; j++)
                parts.push(ss.parts[j]);
        }
        var blob = new Blob(parts, { type: type || 'application/octet-stream' });
        var audioSel = null;
        for (var ai = 0; ai < items.length; ai++) {
            var ab = items[ai];
            if (ab && ab !== selected && sourceBuffers.get(ab).type.indexOf('audio/') === 0) {
                audioSel = ab;
                break;
            }
        }
        var oldUrl = s.url;
        var eos = s.readyState === 'ended';
        if (s.objectURL &&
            (bytes !== s.bytes ||
             (eos && s.url.indexOf('&eos') < 0))) {
            s.bytes = bytes;
            s.version++;
            s.url = s.objectURL + '#ndms=' + s.version +
                    (eos ? '&eos=1' : '');
        }
        var ok = !!global.__ndUpdateBlobURL(s.url, blob);
        if (s.objectURL && s.objectURL !== s.url)
            global.__ndUpdateBlobURL(s.objectURL, blob);
        if (audioSel && sourceBuffers.get(audioSel).parts.length && s.objectURL) {
            var audioState = sourceBuffers.get(audioSel);
            var audioBlob = new Blob(audioState.parts, { type: audioState.type });
            s.audioUrl = s.objectURL + '#ndmsa=' + s.version;
            global.__ndUpdateBlobURL(s.audioUrl, audioBlob);
        }
        mediaSourceScheduleRetarget(ms, oldUrl);
        return ok;
    }

    function mediaSourceScheduleRetarget(ms, oldUrl) {
        var s = mediaSources.get(ms);
        var now = Date.now();
        var last = s.lastRetarget || 0;
        var wait = 2500 - (now - last);
        if (s.readyState === 'ended' || !last || wait <= 0) {
            if (s.retargetTimer) {
                clearTimeout(s.retargetTimer);
                s.retargetTimer = 0;
            }
            var from = s.retargetFrom || oldUrl;
            s.retargetFrom = '';
            s.lastRetarget = now;
            ndRetargetMediaSourceUrl(from, s.url, s.audioUrl);
            return;
        }
        if (!s.retargetFrom) s.retargetFrom = oldUrl;
        if (s.retargetTimer) return;
        s.retargetTimer = setTimeout(function () {
            s.retargetTimer = 0;
            var deferredFrom = s.retargetFrom;
            s.retargetFrom = '';
            s.lastRetarget = Date.now();
            ndRetargetMediaSourceUrl(deferredFrom, s.url, s.audioUrl);
        }, wait);
    }

    function mediaSourceAttach(ms, createEmptyBlobURL) {
        var s = mediaSources.get(ms);
        var url;
        if (ndMseNative) {
            s.mseId = ++ndMseNextId;
            url = 'blob:nd-mse/' + s.mseId;
            s.url = url;
            s.objectURL = url;
        } else {
            url = createEmptyBlobURL();
            s.url = url;
            s.objectURL = url;
            mediaSourceRefreshBlob(ms);
        }
        ndMediaTask(function () { mediaSourceOpen(ms); });
        return url;
    }

    class MediaSource {
        constructor() {
            mediaSources.set(this, idlHandlerState({
                sourceBuffers: newSourceBufferList(),
                activeSourceBuffers: newSourceBufferList(),
                readyState: 'closed', duration: NaN, url: '', objectURL: '',
                version: 0, bytes: 0, mseId: 0, handle: null, audioUrl: '',
                retargetTimer: 0, retargetFrom: '', lastRetarget: 0
            }));
        }
        static get canConstructInDedicatedWorker() { return true; }
        static isTypeSupported(type) {
            idlNeed(arguments, 1, 'MediaSource', 'isTypeSupported');
            return ndSupportedMediaType(type);
        }
        get sourceBuffers() { return mediaSourceOf(this).sourceBuffers; }
        get activeSourceBuffers() { return mediaSourceOf(this).activeSourceBuffers; }
        get readyState() { return mediaSourceOf(this).readyState; }
        get duration() { return mediaSourceOf(this).duration; }
        set duration(value) {
            var s = mediaSourceOf(this);
            idlNeed(arguments, 1, 'MediaSource', 'duration');
            value = Number(value);
            if (isNaN(value) || value < 0)
                throw new TypeError("Failed to set the 'duration' property on 'MediaSource': The value provided is not valid.");
            if (s.readyState !== 'open')
                throw ndDomError('InvalidStateError', "Failed to set the 'duration' property on 'MediaSource': The MediaSource's readyState is not 'open'.");
            var items = sourceBufferLists.get(s.sourceBuffers).items;
            for (var i = 0; i < items.length; i++)
                if (sourceBuffers.get(items[i]).updating)
                    throw ndDomError('InvalidStateError', "Failed to set the 'duration' property on 'MediaSource': One or more SourceBuffers are updating.");
            mediaSourceSetDuration(this, value);
        }
        get handle() {
            var s = mediaSourceOf(this);
            if (!s.handle) {
                s.handle = Object.create(MediaSourceHandle.prototype);
                mediaSourceHandles.set(s.handle, { mediaSource: this });
            }
            return s.handle;
        }
        addSourceBuffer(type) {
            var s = mediaSourceOf(this);
            idlNeed(arguments, 1, 'MediaSource', 'addSourceBuffer');
            type = String(type);
            if (!type)
                throw new TypeError("Failed to execute 'addSourceBuffer' on 'MediaSource': The type provided is empty.");
            if (s.readyState !== 'open')
                throw ndDomError('InvalidStateError', "Failed to execute 'addSourceBuffer' on 'MediaSource': The MediaSource's readyState is not 'open'.");
            if (!ndSupportedMediaType(type))
                throw ndDomError('NotSupportedError', "Failed to execute 'addSourceBuffer' on 'MediaSource': The type provided ('" + type + "') is unsupported.");
            var buffer = newSourceBuffer(this, type);
            pushSourceBuffer(s.sourceBuffers, buffer);
            pushSourceBuffer(s.activeSourceBuffers, buffer);
            mediaSourceRefreshBlob(this);
            return buffer;
        }
        removeSourceBuffer(sourceBuffer) {
            var s = mediaSourceOf(this);
            idlNeed(arguments, 1, 'MediaSource', 'removeSourceBuffer');
            if (!sourceBuffers.has(sourceBuffer))
                throw new TypeError("Failed to execute 'removeSourceBuffer' on 'MediaSource': parameter 1 is not of type 'SourceBuffer'.");
            if (sourceBufferLists.get(s.sourceBuffers).items.indexOf(sourceBuffer) < 0)
                throw ndDomError('NotFoundError', "Failed to execute 'removeSourceBuffer' on 'MediaSource': The SourceBuffer provided is not contained in this MediaSource.");
            abortSourceBufferUpdate(sourceBuffer);
            dropSourceBuffer(s.sourceBuffers, sourceBuffer);
            dropSourceBuffer(s.activeSourceBuffers, sourceBuffer);
            sourceBuffers.get(sourceBuffer).removed = true;
            mediaSourceRefreshBlob(this);
        }
        endOfStream(error = undefined) {
            var s = mediaSourceOf(this);
            if (error !== undefined) {
                error = String(error);
                if (error !== 'network' && error !== 'decode')
                    throw new TypeError("Failed to execute 'endOfStream' on 'MediaSource': The provided value '" + error + "' is not a valid enum value of type EndOfStreamError.");
            }
            if (s.readyState !== 'open')
                throw ndDomError('InvalidStateError', "Failed to execute 'endOfStream' on 'MediaSource': The MediaSource's readyState is not 'open'.");
            var items = sourceBufferLists.get(s.sourceBuffers).items;
            for (var i = 0; i < items.length; i++)
                if (sourceBuffers.get(items[i]).updating)
                    throw ndDomError('InvalidStateError', "Failed to execute 'endOfStream' on 'MediaSource': One or more SourceBuffers are updating.");
            s.readyState = 'ended';
            if (ndMseNative && s.mseId)
                global.__ndMseEos(s.mseId);
            else
                mediaSourceRefreshBlob(this);
            idlFireEvent(this, 'sourceended');
        }
        setLiveSeekableRange(start, end) {
            var s = mediaSourceOf(this);
            idlNeed(arguments, 2, 'MediaSource', 'setLiveSeekableRange');
            start = Number(start);
            end = Number(end);
            if (s.readyState !== 'open')
                throw ndDomError('InvalidStateError', "Failed to execute 'setLiveSeekableRange' on 'MediaSource': The MediaSource's readyState is not 'open'.");
            if (isNaN(start) || isNaN(end) || start < 0 || start > end)
                throw new TypeError("Failed to execute 'setLiveSeekableRange' on 'MediaSource': The provided range is not valid.");
            s.liveSeekableRange = { start: start, end: end };
        }
        clearLiveSeekableRange() {
            var s = mediaSourceOf(this);
            if (s.readyState !== 'open')
                throw ndDomError('InvalidStateError', "Failed to execute 'clearLiveSeekableRange' on 'MediaSource': The MediaSource's readyState is not 'open'.");
            s.liveSeekableRange = null;
        }
    }
    if (!ndWorkerScope) delete MediaSource.prototype.handle;
    idlEventHandlers(MediaSource.prototype, ['sourceopen', 'sourceended', 'sourceclose'], mediaSourceOf);

    function newSourceBuffer(mediaSource, type, proto) {
        var buffer = Object.create(proto || SourceBuffer.prototype);
        sourceBuffers.set(buffer, idlHandlerState({
            updating: false, mode: 'segments', timestampOffset: 0,
            appendWindowStart: 0, appendWindowEnd: Infinity,
            mediaSource: mediaSource, type: type.split(';')[0].trim().toLowerCase(),
            fullType: type, parts: [], removed: false, bytes: 0,
            buffered: new ndTimeRanges(0, 0), taskSeq: 0
        }));
        return buffer;
    }

    function sourceBufferKind(s) {
        return s.type.indexOf('audio/') === 0 ? 'a' : 'v';
    }

    function sourceBufferBytes(s) {
        var ms = s.mediaSource && mediaSources.get(s.mediaSource);
        if (ndMseNative && ms && ms.mseId &&
            typeof global.__ndMseBytes === 'function') {
            var live = Number(global.__ndMseBytes(ms.mseId, sourceBufferKind(s)));
            if (live >= 0) return live;
        }
        return s.bytes;
    }

    function assertSourceBufferMutable(s) {
        if (s.removed || !s.mediaSource)
            throw ndDomError('InvalidStateError', 'This SourceBuffer has been removed from the parent media source.');
        if (s.updating)
            throw ndDomError('InvalidStateError', 'This SourceBuffer is still processing an append or remove operation.');
    }

    function abortSourceBufferUpdate(buffer) {
        var s = sourceBuffers.get(buffer);
        s.taskSeq++;
        if (!s.updating) return;
        s.updating = false;
        ndMediaTask(function () {
            idlFireEvent(buffer, 'abort');
            idlFireEvent(buffer, 'updateend');
        });
    }

    function sourceBufferUpdate(buffer, s, work) {
        s.updating = true;
        var seq = ++s.taskSeq;
        ndMediaTask(function () {
            if (seq === s.taskSeq) idlFireEvent(buffer, 'updatestart');
        });
        ndMediaTask(function () {
            if (seq === s.taskSeq) work();
        });
    }

    class SourceBuffer {
        constructor() { throw idlIllegalConstructor('SourceBuffer'); }
        get mode() { return sourceBufferOf(this).mode; }
        set mode(value) {
            var s = sourceBufferOf(this);
            idlNeed(arguments, 1, 'SourceBuffer', 'mode');
            value = String(value);
            if (value !== 'segments' && value !== 'sequence') return;
            assertSourceBufferMutable(s);
            mediaSourceReopen(s.mediaSource);
            s.mode = value;
        }
        get updating() { return sourceBufferOf(this).updating; }
        get buffered() {
            var s = sourceBufferOf(this);
            if (s.removed || !s.mediaSource)
                throw ndDomError('InvalidStateError', "Failed to read the 'buffered' property from 'SourceBuffer': This SourceBuffer has been removed from the parent media source.");
            var ms = mediaSources.get(s.mediaSource);
            if (ndMseNative && ms && ms.mseId &&
                typeof global.__ndMseBuffered === 'function') {
                var kind = sourceBufferKind(s);
                var end = global.__ndMseBuffered(ms.mseId, kind);
                var start = typeof global.__ndMseBufferedStart === 'function' ?
                    global.__ndMseBufferedStart(ms.mseId, kind) : 0;
                return new ndTimeRanges(start >= 0 ? start : 0,
                                        end > start ? end : 0);
            }
            return s.buffered;
        }
        get timestampOffset() { return sourceBufferOf(this).timestampOffset; }
        set timestampOffset(value) {
            var s = sourceBufferOf(this);
            idlNeed(arguments, 1, 'SourceBuffer', 'timestampOffset');
            value = Number(value);
            if (!isFinite(value))
                throw new TypeError("Failed to set the 'timestampOffset' property on 'SourceBuffer': The provided double value is non-finite.");
            assertSourceBufferMutable(s);
            mediaSourceReopen(s.mediaSource);
            s.timestampOffset = value;
        }
        get appendWindowStart() { return sourceBufferOf(this).appendWindowStart; }
        set appendWindowStart(value) {
            var s = sourceBufferOf(this);
            idlNeed(arguments, 1, 'SourceBuffer', 'appendWindowStart');
            value = Number(value);
            if (!isFinite(value))
                throw new TypeError("Failed to set the 'appendWindowStart' property on 'SourceBuffer': The provided double value is non-finite.");
            assertSourceBufferMutable(s);
            if (value < 0 || value >= s.appendWindowEnd)
                throw new TypeError("Failed to set the 'appendWindowStart' property on 'SourceBuffer': The provided value is not valid.");
            s.appendWindowStart = value;
        }
        get appendWindowEnd() { return sourceBufferOf(this).appendWindowEnd; }
        set appendWindowEnd(value) {
            var s = sourceBufferOf(this);
            idlNeed(arguments, 1, 'SourceBuffer', 'appendWindowEnd');
            value = Number(value);
            if (isNaN(value))
                throw new TypeError("Failed to set the 'appendWindowEnd' property on 'SourceBuffer': The provided double value is non-finite.");
            assertSourceBufferMutable(s);
            if (value <= s.appendWindowStart)
                throw new TypeError("Failed to set the 'appendWindowEnd' property on 'SourceBuffer': The provided value is not valid.");
            s.appendWindowEnd = value;
        }
        appendBuffer(data) {
            var s = sourceBufferOf(this);
            idlNeed(arguments, 1, 'SourceBuffer', 'appendBuffer');
            var copy = ndBufferSourceBytes(data);
            var ms = s.mediaSource && mediaSources.get(s.mediaSource);
            if (s.removed || !ms || ms.readyState === 'closed' || s.updating)
                throw ndDomError('InvalidStateError', "Failed to execute 'appendBuffer' on 'SourceBuffer': This SourceBuffer is not in a state to accept appends.");
            mediaSourceReopen(s.mediaSource);
            if (sourceBufferBytes(s) + copy.length > ndSourceBufferQuota)
                throw ndDomError('QuotaExceededError',
                                 'SourceBuffer is full; remove buffered media first');
            var buffer = this;
            sourceBufferUpdate(buffer, s, function () {
                var ok = true;
                if (copy.length === 0) {
                    ok = true;
                } else if (ndMseNative && ms.mseId) {
                    ok = !!global.__ndMseAppend(ms.mseId, sourceBufferKind(s), copy);
                } else {
                    s.parts.push(copy);
                }
                s.updating = false;
                if (!ok) {
                    idlFireEvent(buffer, 'error');
                    idlFireEvent(buffer, 'updateend');
                    mediaSourceDecodeError(s.mediaSource);
                    return;
                }
                s.bytes += copy.length;
                if (ndMseNative && ms.mseId &&
                    typeof global.__ndMseBuffered === 'function') {
                    var nativeEnd = Number(global.__ndMseBuffered(ms.mseId, sourceBufferKind(s)));
                    if (nativeEnd > 0 && (isNaN(ms.duration) || nativeEnd > ms.duration))
                        ms.duration = nativeEnd;
                } else {
                    var seconds = s.bytes > 0 ? Math.max(0.001, s.bytes / 262144) : 0;
                    s.buffered = new ndTimeRanges(0, seconds);
                    if (isNaN(ms.duration) || seconds > ms.duration)
                        ms.duration = seconds;
                    mediaSourceRefreshBlob(s.mediaSource);
                }
                idlFireEvent(buffer, 'update');
                idlFireEvent(buffer, 'updateend');
            });
        }
        abort() {
            var s = sourceBufferOf(this);
            var ms = s.mediaSource && mediaSources.get(s.mediaSource);
            if (s.removed || !ms || ms.readyState !== 'open')
                throw ndDomError('InvalidStateError', "Failed to execute 'abort' on 'SourceBuffer': This SourceBuffer is not attached to an open MediaSource.");
            abortSourceBufferUpdate(this);
            s.appendWindowStart = 0;
            s.appendWindowEnd = Infinity;
        }
        changeType(type) {
            var s = sourceBufferOf(this);
            idlNeed(arguments, 1, 'SourceBuffer', 'changeType');
            type = String(type);
            if (!type)
                throw new TypeError("Failed to execute 'changeType' on 'SourceBuffer': The type provided is empty.");
            assertSourceBufferMutable(s);
            if (!ndSupportedMediaType(type))
                throw ndDomError('NotSupportedError', "Failed to execute 'changeType' on 'SourceBuffer': The type provided ('" + type + "') is not supported.");
            mediaSourceReopen(s.mediaSource);
            s.fullType = type;
            s.type = type.split(';')[0].trim().toLowerCase();
            mediaSourceRefreshBlob(s.mediaSource);
        }
        remove(start, end) {
            var s = sourceBufferOf(this);
            idlNeed(arguments, 2, 'SourceBuffer', 'remove');
            assertSourceBufferMutable(s);
            var ms = mediaSources.get(s.mediaSource);
            var duration = Number(ms.duration);
            start = Number(start);
            end = Number(end);
            if (isNaN(duration) || isNaN(start) || isNaN(end))
                throw new TypeError("Failed to execute 'remove' on 'SourceBuffer': The removal range is not valid.");
            if (start < 0 || start > duration || end <= start)
                throw new TypeError("Failed to execute 'remove' on 'SourceBuffer': The removal range is not valid.");
            mediaSourceReopen(s.mediaSource);
            var buffer = this;
            sourceBufferUpdate(buffer, s, function () {
                var removed = true;
                if (ndMseNative && ms.mseId &&
                    typeof global.__ndMseRemove === 'function')
                    removed = !!global.__ndMseRemove(ms.mseId, sourceBufferKind(s), start, end);
                if (start <= 0 && end > 0 && !(ndMseNative && ms.mseId)) {
                    s.parts = [];
                    s.bytes = 0;
                    s.buffered = new ndTimeRanges(0, 0);
                }
                s.updating = false;
                if (removed) s.bytes = sourceBufferBytes(s);
                if (!(ndMseNative && ms.mseId))
                    mediaSourceRefreshBlob(s.mediaSource);
                idlFireEvent(buffer, 'update');
                idlFireEvent(buffer, 'updateend');
            });
        }
    }
    idlEventHandlers(SourceBuffer.prototype,
                     ['updatestart', 'update', 'updateend', 'error', 'abort'], sourceBufferOf);

    class ManagedMediaSource extends MediaSource {
        constructor() {
            super();
        }
        get streaming() { return mediaSourceOf(this).readyState === 'open'; }
    }
    idlEventHandlers(ManagedMediaSource.prototype,
                     ['startstreaming', 'endstreaming', 'qualitychange'], mediaSourceOf);

    class ManagedSourceBuffer extends SourceBuffer {
        constructor() { throw idlIllegalConstructor('ManagedSourceBuffer'); }
    }
    idlEventHandlers(ManagedSourceBuffer.prototype, ['bufferedchange'], sourceBufferOf);

    idlExpose(SourceBufferList, 'SourceBufferList', idlEventTarget());
    idlExpose(MediaSourceHandle, 'MediaSourceHandle', null);
    idlExpose(MediaSource, 'MediaSource', idlEventTarget());
    idlExpose(SourceBuffer, 'SourceBuffer', idlEventTarget());
    idlExpose(ManagedMediaSource, 'ManagedMediaSource', MediaSource);
    idlExpose(ManagedSourceBuffer, 'ManagedSourceBuffer', SourceBuffer);

    function ndRandomId(prefix) {
        return prefix + '-' + Math.random().toString(36).slice(2) + '-' +
               (ndRandomId.counter = (ndRandomId.counter || 0) + 1);
    }

    function MediaStreamTrack() {
        if (!(this instanceof MediaStreamTrack))
            throw new TypeError('Illegal constructor');
        this.kind = 'video';
        this.id = ndRandomId('track');
        this.label = '';
        this.enabled = true;
        this.muted = false;
        this.readyState = 'live';
        this.contentHint = '';
        this.onended = null;
        this.onmute = null;
        this.onunmute = null;
    }
    ndEventMethods(MediaStreamTrack.prototype);
    MediaStreamTrack.prototype.getSettings = function () { return {}; };
    MediaStreamTrack.prototype.getCapabilities = function () { return {}; };
    MediaStreamTrack.prototype.getConstraints = function () { return {}; };
    MediaStreamTrack.prototype.applyConstraints = function () {
        return global.Promise.resolve();
    };
    MediaStreamTrack.prototype.clone = function () {
        var copy = Object.create(Object.getPrototypeOf(this));
        MediaStreamTrack.call(copy);
        copy.kind = this.kind;
        copy.label = this.label;
        return copy;
    };
    MediaStreamTrack.prototype.stop = function () {
        if (this.readyState === 'ended') return;
        this.readyState = 'ended';
        ndFireEvent(this, 'ended');
    };

    function MediaStream(source) {
        if (!(this instanceof MediaStream)) return new MediaStream(source);
        this._tracks = [];
        this.id = ndRandomId('stream');
        this.onaddtrack = null;
        this.onremovetrack = null;
        if (source && typeof source.getTracks === 'function')
            this._tracks = source.getTracks();
        else if (source && typeof source.length === 'number')
            for (var i = 0; i < source.length; i++) this._tracks.push(source[i]);
    }
    ndEventMethods(MediaStream.prototype);
    ndAccessors(MediaStream.prototype, {
        active: function () {
            for (var i = 0; i < this._tracks.length; i++)
                if (this._tracks[i].readyState !== 'ended') return true;
            return false;
        }
    });
    MediaStream.prototype.getTracks = function () {
        return this._tracks.slice();
    };
    MediaStream.prototype.getVideoTracks = function () {
        return this._tracks.filter(function (t) { return t.kind === 'video'; });
    };
    MediaStream.prototype.getAudioTracks = function () {
        return this._tracks.filter(function (t) { return t.kind === 'audio'; });
    };
    MediaStream.prototype.getTrackById = function (id) {
        id = String(id);
        for (var i = 0; i < this._tracks.length; i++)
            if (this._tracks[i].id === id) return this._tracks[i];
        return null;
    };
    MediaStream.prototype.addTrack = function (track) {
        if (this._tracks.indexOf(track) >= 0) return;
        this._tracks.push(track);
        ndFireEvent(this, 'addtrack');
    };
    MediaStream.prototype.removeTrack = function (track) {
        var i = this._tracks.indexOf(track);
        if (i < 0) return;
        this._tracks.splice(i, 1);
        ndFireEvent(this, 'removetrack');
    };
    MediaStream.prototype.clone = function () {
        return new MediaStream(this._tracks.map(function (t) {
            return t.clone();
        }));
    };

    replaceCtor('MediaStream', MediaStream);
    replaceCtor('MediaStreamTrack', MediaStreamTrack);
    global.__ndMediaStream = MediaStream;
    global.__ndMediaStreamTrack = MediaStreamTrack;

    function RTCSessionDescription(init) {
        if (!(this instanceof RTCSessionDescription))
            return new RTCSessionDescription(init);
        init = init || {};
        this.type = init.type === undefined ? null : String(init.type);
        this.sdp = init.sdp === undefined ? '' : String(init.sdp);
    }
    RTCSessionDescription.prototype.toJSON = function () {
        return { type: this.type, sdp: this.sdp };
    };

    function RTCIceCandidate(init) {
        if (!(this instanceof RTCIceCandidate))
            return new RTCIceCandidate(init);
        if (typeof init === 'string') init = { candidate: init };
        init = init || {};
        if (init.sdpMid === undefined && init.sdpMLineIndex === undefined)
            throw new TypeError(
                'RTCIceCandidate requires sdpMid or sdpMLineIndex');
        this.candidate = init.candidate === undefined ? ''
                                                      : String(init.candidate);
        this.sdpMid = init.sdpMid === undefined ? null : String(init.sdpMid);
        this.sdpMLineIndex = init.sdpMLineIndex === undefined ? null
                                                : Number(init.sdpMLineIndex);
        this.usernameFragment = init.usernameFragment === undefined ? null
                                        : String(init.usernameFragment);
    }
    RTCIceCandidate.prototype.toJSON = function () {
        return {
            candidate: this.candidate, sdpMid: this.sdpMid,
            sdpMLineIndex: this.sdpMLineIndex,
            usernameFragment: this.usernameFragment
        };
    };

    function RTCRtpSender(track) {
        this.track = track || null;
        this.dtmf = null;
        this.transport = null;
    }
    RTCRtpSender.prototype.getParameters = function () {
        return { encodings: [], codecs: [], headerExtensions: [],
                 rtcp: {}, transactionId: '' };
    };
    RTCRtpSender.prototype.setParameters = function () {
        return global.Promise.resolve();
    };
    RTCRtpSender.prototype.getStats = function () {
        return global.Promise.resolve(new global.Map());
    };
    RTCRtpSender.prototype.replaceTrack = function (track) {
        this.track = track || null;
        return global.Promise.resolve();
    };
    RTCRtpSender.getCapabilities = function () {
        return { codecs: [], headerExtensions: [] };
    };

    function RTCRtpReceiver(track) {
        this.track = track || null;
        this.transport = null;
    }
    RTCRtpReceiver.prototype.getParameters = function () {
        return { codecs: [], headerExtensions: [], rtcp: {} };
    };
    RTCRtpReceiver.prototype.getContributingSources = function () { return []; };
    RTCRtpReceiver.prototype.getSynchronizationSources = function () { return []; };
    RTCRtpReceiver.prototype.getStats = function () {
        return global.Promise.resolve(new global.Map());
    };
    RTCRtpReceiver.getCapabilities = RTCRtpSender.getCapabilities;

    function RTCRtpTransceiver(kind, track) {
        this.mid = null;
        this.direction = 'sendrecv';
        this.currentDirection = null;
        this.sender = new RTCRtpSender(track);
        this.receiver = new RTCRtpReceiver(null);
        this._kind = kind;
    }
    RTCRtpTransceiver.prototype.stop = function () {
        this.currentDirection = 'stopped';
    };
    RTCRtpTransceiver.prototype.setCodecPreferences = function () {};

    replaceCtor('RTCSessionDescription', RTCSessionDescription);
    replaceCtor('RTCIceCandidate', RTCIceCandidate);
    replaceCtor('RTCRtpSender', RTCRtpSender);
    replaceCtor('RTCRtpReceiver', RTCRtpReceiver);
    replaceCtor('RTCRtpTransceiver', RTCRtpTransceiver);

    if (typeof global.RTCPeerConnection === 'function') {
        var pcProto = global.RTCPeerConnection.prototype;
        defineMethod(pcProto, 'addTrack', function (track) {
            if (!this._ndSenders) this._ndSenders = [];
            var sender = new RTCRtpSender(track);
            this._ndSenders.push(sender);
            return sender;
        });
        defineMethod(pcProto, 'removeTrack', function (sender) {
            var list = this._ndSenders || [];
            var i = list.indexOf(sender);
            if (i >= 0) list.splice(i, 1);
            if (sender) sender.track = null;
        });
        defineMethod(pcProto, 'addTransceiver', function (trackOrKind) {
            if (!this._ndTransceivers) this._ndTransceivers = [];
            var isTrack = trackOrKind && typeof trackOrKind === 'object';
            var transceiver = new RTCRtpTransceiver(
                isTrack ? trackOrKind.kind : String(trackOrKind || 'video'),
                isTrack ? trackOrKind : null);
            this._ndTransceivers.push(transceiver);
            return transceiver;
        });
        defineMethod(pcProto, 'getConfiguration', function () {
            return this._configuration || {};
        });
        defineMethod(pcProto, 'setConfiguration', function (config) {
            this._configuration = config || {};
        });
        defineMethod(pcProto, 'restartIce', function () {});
        pcProto.getSenders = function () {
            return (this._ndSenders || []).slice();
        };
        pcProto.getReceivers = function () {
            return (this._ndTransceivers || []).map(function (t) {
                return t.receiver;
            });
        };
        pcProto.getTransceivers = function () {
            return (this._ndTransceivers || []).slice();
        };
        if (typeof global.RTCPeerConnection.generateCertificate !== 'function')
            global.RTCPeerConnection.generateCertificate = function () {
                return global.Promise.resolve({
                    expires: Date.now() + 2592000000,
                    getFingerprints: function () { return []; }
                });
            };
    }

    if (typeof global.URL === 'function' &&
        typeof global.URL.createObjectURL === 'function' &&
        !global.URL.__ndMediaSourceObjectURL) {
        var ndCreateObjectURL = global.URL.createObjectURL;
        var ndRevokeObjectURL = global.URL.revokeObjectURL;
        global.URL.createObjectURL = function (obj) {
            if (mediaSources.has(obj)) {
                var self = this;
                return mediaSourceAttach(obj, function () {
                    return ndCreateObjectURL.call(self,
                        new Blob([], { type: 'application/octet-stream' }));
                });
            }
            return ndCreateObjectURL.apply(this, arguments);
        };
        global.URL.revokeObjectURL = function (url) {
            return ndRevokeObjectURL.apply(this, arguments);
        };
        global.URL.__ndMediaSourceObjectURL = true;
    }

    if (typeof global.URL === 'function' &&
        typeof global.URL.canParse !== 'function') {
        global.URL.canParse = function (url, base) {
            try { new global.URL(url, base); return true; }
            catch (e) { return false; }
        };
    }
    if (typeof global.URL === 'function' &&
        typeof global.URL.parse !== 'function') {
        global.URL.parse = function (url, base) {
            try { return new global.URL(url, base); }
            catch (e) { return null; }
        };
    }

    function XMLSerializer() {
        if (!(this instanceof XMLSerializer)) return new XMLSerializer();
    }
    function xmlSerializeNode(node) {
        if (!node) return '';
        if (node.nodeType === 3) return String(node.nodeValue == null ? '' : node.nodeValue);
        if (node.nodeType === 8) return '<!--' + String(node.nodeValue || '') + '-->';
        if (typeof node.outerHTML === 'string') return node.outerHTML;
        if (node.nodeType === 9) {
            if (node.documentElement &&
                typeof node.documentElement.outerHTML === 'string')
                return node.documentElement.outerHTML;
        }
        if (node.nodeType === 11) {
            var s = '';
            var c = node.firstChild;
            while (c) { s += xmlSerializeNode(c); c = c.nextSibling; }
            return s;
        }
        if (typeof node.innerHTML === 'string') return node.innerHTML;
        return String(node);
    }
    XMLSerializer.prototype.serializeToString = function (node) {
        return xmlSerializeNode(node);
    };
    defineCtor('XMLSerializer', XMLSerializer);

    function AbortSignal() {
        if (!(this instanceof AbortSignal)) return new AbortSignal();
        this.aborted = false;
        this.reason = undefined;
        this._cbs = [];
        this.onabort = null;
    }
    if (typeof global.EventTarget === 'function' && global.EventTarget.prototype) {
        try { Object.setPrototypeOf(AbortSignal.prototype, global.EventTarget.prototype); }
        catch (e) {}
    }
    AbortSignal.prototype.addEventListener = function (type, cb) {
        if (type !== 'abort' || typeof cb !== 'function') return;
        this._cbs.push(cb);
    };
    AbortSignal.prototype.removeEventListener = function (type, cb) {
        if (type !== 'abort') return;
        var i = this._cbs.indexOf(cb);
        if (i >= 0) this._cbs.splice(i, 1);
    };
    AbortSignal.prototype.dispatchEvent = function (ev) {
        if (ev && ev.type === 'abort') this._fire(ev);
        return true;
    };
    AbortSignal.prototype.throwIfAborted = function () {
        if (this.aborted) {
            var r = this.reason;
            if (r === undefined) {
                var e = typeof DOMException === 'function'
                    ? new DOMException('This operation was aborted', 'AbortError')
                    : new Error('AbortError');
                e.name = 'AbortError';
                r = e;
            }
            throw r;
        }
    };
    AbortSignal.prototype._fire = function (ev) {
        if (typeof this.onabort === 'function') {
            try { this.onabort.call(this, ev); } catch (e) {}
        }
        var cbs = this._cbs.slice();
        for (var i = 0; i < cbs.length; i++) {
            try { cbs[i].call(this, ev); } catch (e) {}
        }
    };
    AbortSignal.abort = function (reason) {
        var s = new AbortSignal();
        s.aborted = true;
        s.reason = reason === undefined
            ? (typeof DOMException === 'function'
                ? new DOMException('This operation was aborted', 'AbortError')
                : new Error('AbortError'))
            : reason;
        return s;
    };
    AbortSignal.timeout = function (ms) {
        var s = new AbortSignal();
        setTimeout(function () {
            if (!s.aborted) {
                s.aborted = true;
                var e = typeof DOMException === 'function'
                    ? new DOMException('The operation timed out.', 'TimeoutError')
                    : new Error('TimeoutError');
                e.name = 'TimeoutError';
                s.reason = e;
                s._fire({type: 'abort', target: s});
            }
        }, ms);
        return s;
    };
    AbortSignal.any = function (signals) {
        var s = new AbortSignal();
        function onAny(src) {
            if (s.aborted) return;
            s.aborted = true;
            s.reason = src ? src.reason : undefined;
            s._fire({type: 'abort', target: s});
        }
        if (signals) {
            var sigList = Array.isArray(signals) ? signals : Array.from(signals);
            for (var i = 0; i < sigList.length; i++) {
                var sig = sigList[i];
                if (!sig) continue;
                if (sig.aborted) { onAny(sig); break; }
                (function (item) {
                    if (typeof item.addEventListener === 'function')
                        item.addEventListener('abort', function () { onAny(item); });
                })(sig);
            }
        }
        return s;
    };
    defineCtor('AbortSignal', AbortSignal);

    function AbortController() {
        if (!(this instanceof AbortController)) return new AbortController();
        this.signal = new AbortSignal();
    }
    AbortController.prototype.abort = function (reason) {
        var s = this.signal;
        if (s.aborted) return;
        s.aborted = true;
        s.reason = reason === undefined ? new Error('AbortError') : reason;
        s._fire({type: 'abort', target: s});
    };
    defineCtor('AbortController', AbortController);

    try {
        var probe = global.document && global.document.createElement('div');
        var eventTargetProto = probe && Object.getPrototypeOf(probe);
        if (eventTargetProto) {
            var origAEL = eventTargetProto.addEventListener;
            if (typeof origAEL === 'function') {
                eventTargetProto.addEventListener = function (type, cb, opts) {
                    if (opts && typeof opts === 'object' && opts.signal) {
                        var sig = opts.signal;
                        if (sig && sig.aborted) return;
                        var self = this;
                        origAEL.call(self, type, cb, opts);
                        if (sig && typeof sig.addEventListener === 'function') {
                            sig.addEventListener('abort', function once() {
                                self.removeEventListener(type, cb, opts);
                            });
                        }
                        return;
                    }
                    return origAEL.call(this, type, cb, opts);
                };
            }
        }
    } catch (e) { /* ignore */ }

    if (!global.NodeFilter || typeof global.NodeFilter.SHOW_ALL !== 'number') {
        var NF = global.NodeFilter || {};
        NF.SHOW_ALL                  = 0xFFFFFFFF;
        NF.SHOW_ELEMENT              = 0x1;
        NF.SHOW_ATTRIBUTE            = 0x2;
        NF.SHOW_TEXT                 = 0x4;
        NF.SHOW_CDATA_SECTION        = 0x8;
        NF.SHOW_ENTITY_REFERENCE     = 0x10;
        NF.SHOW_ENTITY               = 0x20;
        NF.SHOW_PROCESSING_INSTRUCTION = 0x40;
        NF.SHOW_NOTATION             = 0x800;
        NF.SHOW_COMMENT              = 0x80;
        NF.SHOW_DOCUMENT             = 0x100;
        NF.SHOW_DOCUMENT_TYPE        = 0x200;
        NF.SHOW_DOCUMENT_FRAGMENT    = 0x400;
        NF.FILTER_ACCEPT             = 1;
        NF.FILTER_REJECT             = 2;
        NF.FILTER_SKIP               = 3;
        defineCtor('NodeFilter', NF);
    }

    try {
        var doc = global.document;
        if (doc && doc.createElement) {
            var probe = doc.createElement('div');
            var elementProto = Object.getPrototypeOf(probe);
            if (elementProto) {
                var onProps = [
                    'click','dblclick','mousedown','mouseup','mousemove','mouseenter',
                    'mouseleave','mouseover','mouseout','contextmenu','wheel',
                    'keydown','keyup','keypress',
                    'focus','blur','focusin','focusout',
                    'input','change','submit','reset','select',
                    'load','error','abort','loadstart','loadend','progress',
                    'animationstart','animationend','animationiteration',
                    'transitionstart','transitionend','transitionrun','transitioncancel',
                    'webkitanimationstart','webkitanimationend','webkitanimationiteration',
                    'webkittransitionend',
                    'pointerdown','pointerup','pointermove','pointerenter',
                    'pointerleave','pointerover','pointerout','pointercancel',
                    'touchstart','touchend','touchmove','touchcancel',
                    'drag','dragstart','dragend','dragenter','dragleave','dragover','drop',
                    'scroll','resize',
                    'copy','cut','paste',
                    'beforeinput','compositionstart','compositionend','compositionupdate',
                    'invalid'
                ];
                function makeOnAccessor(propName) {
                    var slot = Symbol.for('nd.on.' + propName);
                    var cslot = Symbol.for('nd.onc.' + propName);
                    var sslot = Symbol.for('nd.ons.' + propName);
                    return {
                        configurable: true, enumerable: false,
                        get: function () {
                            var h = this[slot];
                            if (h) return h;
                            if (!this || typeof this.getAttribute !== 'function')
                                return null;
                            var code = this.getAttribute(propName);
                            if (code == null) return null;
                            if (this[sslot] === code && this[cslot])
                                return this[cslot];
                            var fn;
                            try { fn = new Function('event', code); }
                            catch (e) { return null; }
                            this[cslot] = fn;
                            this[sslot] = code;
                            return fn;
                        },
                        set: function (v) {
                            this[slot] = (typeof v === 'function') ? v : null;
                        }
                    };
                }
                for (var i = 0; i < onProps.length; i++) {
                    var p = 'on' + onProps[i];
                    if (Object.getOwnPropertyDescriptor(elementProto, p)) continue;
                    Object.defineProperty(elementProto, p, makeOnAccessor(p));
                }
            }
            if (elementProto) {
                function camelToAttr(key) {
                    return 'data-' + String(key).replace(/[A-Z]/g, function (c) {
                        return '-' + c.toLowerCase();
                    });
                }
                function attrToCamel(name) {
                    return name.slice(5).replace(/-([a-z])/g, function (_, c) {
                        return c.toUpperCase();
                    });
                }
                function defineFrameAccessor(name, getter) {
                    if (Object.getOwnPropertyDescriptor(elementProto, name)) return;
                    var nativeGet = null;
                    for (var anc = Object.getPrototypeOf(elementProto); anc;
                         anc = Object.getPrototypeOf(anc)) {
                        var d = Object.getOwnPropertyDescriptor(anc, name);
                        if (d && d.get) { nativeGet = d.get; break; }
                    }
                    Object.defineProperty(elementProto, name, {
                        configurable: true, get: nativeGet || getter
                    });
                }
                function isFrameElement(el) {
                    var tag = el && el.nodeName ? String(el.nodeName).toLowerCase() : '';
                    return tag === 'iframe' || tag === 'frame' ||
                           tag === 'object' || tag === 'embed';
                }
                defineFrameAccessor('contentDocument', function () {
                    return isFrameElement(this) ? null : null;
                });
                defineFrameAccessor('contentWindow', function () {
                    if (!isFrameElement(this)) return null;
                    return {
                        document: null,
                        location: { href: '', replace: function () {}, assign: function () {} },
                        postMessage: function () {},
                        addEventListener: function () {},
                        removeEventListener: function () {},
                        focus: function () {},
                        blur: function () {},
                        close: function () {},
                        closed: true
                    };
                });
                if (!('dataset' in probe)) {
                    Object.defineProperty(elementProto, 'dataset', {
                        configurable: true,
                        get: function () {
                            var el = this;
                            return new Proxy({}, {
                                get: function (t, key) {
                                    if (typeof key !== 'string') return undefined;
                                    var v = el.getAttribute(camelToAttr(key));
                                    return v == null ? undefined : v;
                                },
                                set: function (t, key, value) {
                                    if (typeof key !== 'string') return false;
                                    if (/-[a-z]/.test(key))
                                        throw new SyntaxError(
                                            "'-' must not be followed by a lowercase " +
                                            'letter in a dataset name');
                                    el.setAttribute(camelToAttr(key), String(value));
                                    return true;
                                },
                                has: function (t, key) {
                                    if (typeof key !== 'string') return false;
                                    return el.hasAttribute(camelToAttr(key));
                                },
                                deleteProperty: function (t, key) {
                                    if (typeof key !== 'string') return false;
                                    el.removeAttribute(camelToAttr(key));
                                    return true;
                                },
                                ownKeys: function () {
                                    var out = [];
                                    var attrs = el.attributes;
                                    var n = attrs ? attrs.length : 0;
                                    for (var i = 0; i < n; i++) {
                                        var nm = attrs[i].name;
                                        if (nm.indexOf('data-') === 0)
                                            out.push(attrToCamel(nm));
                                    }
                                    return out;
                                },
                                getOwnPropertyDescriptor: function (t, key) {
                                    if (typeof key !== 'string') return undefined;
                                    if (!el.hasAttribute(camelToAttr(key))) return undefined;
                                    return {
                                        enumerable: true, configurable: true,
                                        writable: true,
                                        value: el.getAttribute(camelToAttr(key))
                                    };
                                }
                            });
                        }
                    });
                }
            }
        }
    } catch (e) { /* prototype may be locked; tolerate */ }

    function ReadableStream(underlying, strategy) {
        if (!(this instanceof ReadableStream))
            return new ReadableStream(underlying, strategy);
        var self = this;
        self._buf = [];
        self._closed = false;
        self._error = null;
        self._cancelled = false;
        self._waiters = [];
        self.locked = false;
        function wake() {
            var ws = self._waiters;
            self._waiters = [];
            for (var i = 0; i < ws.length; i++) ws[i]();
        }
        var controller = {
            enqueue: function (chunk) {
                if (!self._closed && !self._cancelled) {
                    self._buf.push(chunk);
                    wake();
                }
            },
            close: function () { self._closed = true; wake(); },
            error: function (e) { self._error = e; self._closed = true; wake(); },
            get desiredSize() { return self._closed ? 0 : 1; }
        };
        self._controller = controller;
        if (underlying && typeof underlying.start === 'function') {
            try { underlying.start(controller); } catch (e) { /* ignore */ }
        }
        self._underlying = underlying || {};
    }
    function rsReadOnce(self) {
        if (self._error) return Promise.reject(self._error);
        if (self._buf.length > 0)
            return Promise.resolve({ value: self._buf.shift(), done: false });
        if (self._closed)
            return Promise.resolve({ value: undefined, done: true });
        return new Promise(function (resolve, reject) {
            self._waiters.push(function () {
                if (self._error) { reject(self._error); return; }
                if (self._buf.length > 0)
                    resolve({ value: self._buf.shift(), done: false });
                else
                    resolve({ value: undefined, done: true });
            });
        });
    }
    ReadableStream.prototype.getReader = function () {
        var self = this;
        self.locked = true;
        return {
            read: function () { return rsReadOnce(self); },
            cancel: function () { self._cancelled = true; return Promise.resolve(); },
            releaseLock: function () { self.locked = false; },
            closed: Promise.resolve()
        };
    };
    ReadableStream.prototype.cancel = function () {
        this._cancelled = true; return Promise.resolve();
    };
    ReadableStream.prototype.pipeTo = function (writable) {
        var self = this;
        if (!writable || !writable._writeChunk) return Promise.resolve();
        function pump() {
            return rsReadOnce(self).then(function (r) {
                if (r.done) {
                    if (writable._closeStream) writable._closeStream();
                    return;
                }
                writable._writeChunk(r.value);
                return pump();
            });
        }
        return pump();
    };
    ReadableStream.prototype.pipeThrough = function (transform) {
        if (!transform || !transform.readable) return new ReadableStream();
        if (transform.writable && transform.writable._writeChunk) {
            this.pipeTo(transform.writable);
        }
        return transform.readable;
    };
    ReadableStream.prototype.tee = function () { return [this, this]; };
    if (typeof Symbol !== 'undefined' && Symbol.asyncIterator) {
        ReadableStream.prototype[Symbol.asyncIterator] = function () {
            var self = this;
            return {
                next: function () { return rsReadOnce(self); },
                return: function () { return Promise.resolve({value: undefined, done: true}); }
            };
        };
    }
    if (typeof global.ReadableStream !== 'function' ||
        typeof global.ReadableStream.prototype.getReader !== 'function')
        replaceCtor('ReadableStream', ReadableStream);

    function WritableStream(underlying, strategy) {
        if (!(this instanceof WritableStream))
            return new WritableStream(underlying, strategy);
        var self = this;
        var u = underlying || {};
        self._underlying = u;
        self.locked = false;
        var controller = { error: function () {}, signal: undefined };
        if (typeof u.start === 'function') {
            try { u.start(controller); } catch (e) { /* ignore */ }
        }
        if (typeof u.write === 'function' && !self._writeChunk)
            self._writeChunk = function (chunk) { return u.write(chunk, controller); };
        if (typeof u.close === 'function' && !self._closeStream)
            self._closeStream = function () { return u.close(); };
        if (typeof u.abort === 'function' && !self._abortStream)
            self._abortStream = function (reason) { return u.abort(reason); };
    }
    function wsInvoke(fn, arg) {
        if (typeof fn !== 'function') return Promise.resolve();
        try { return Promise.resolve(fn(arg)); }
        catch (e) { return Promise.reject(e); }
    }
    WritableStream.prototype.getWriter = function () {
        var self = this;
        self.locked = true;
        return {
            write: function (chunk) { return wsInvoke(self._writeChunk, chunk); },
            close: function () { return wsInvoke(self._closeStream); },
            abort: function (reason) { return wsInvoke(self._abortStream, reason); },
            releaseLock: function () { self.locked = false; },
            ready: Promise.resolve(),
            closed: Promise.resolve(),
            desiredSize: 1
        };
    };
    WritableStream.prototype.abort = function (reason) {
        return wsInvoke(this._abortStream, reason);
    };
    WritableStream.prototype.close = function () {
        return wsInvoke(this._closeStream);
    };
    if (typeof global.WritableStream !== 'function' ||
        typeof global.WritableStream.prototype.getWriter !== 'function')
        replaceCtor('WritableStream', WritableStream);

    function TransformStream(transformer, writableStrategy, readableStrategy) {
        if (!(this instanceof TransformStream))
            return new TransformStream(transformer, writableStrategy, readableStrategy);
        var readable = new ReadableStream();
        var writable = new WritableStream();
        var controller = readable._controller;
        var t = transformer || {};
        var transformCtl = {
            enqueue: function (chunk) { controller.enqueue(chunk); },
            terminate: function () { controller.close(); },
            error: function (e) { controller.error(e); }
        };
        writable._writeChunk = function (chunk) {
            if (typeof t.transform === 'function') {
                try { t.transform(chunk, transformCtl); }
                catch (e) { controller.error(e); }
            } else {
                controller.enqueue(chunk);
            }
        };
        writable._closeStream = function () {
            if (typeof t.flush === 'function') {
                try { t.flush(transformCtl); }
                catch (e) { controller.error(e); }
            }
            controller.close();
        };
        this.readable = readable;
        this.writable = writable;
        if (typeof t.start === 'function') {
            try { t.start(transformCtl); } catch (e) { /* ignore */ }
        }
    }
    defineCtor('TransformStream', TransformStream);

    function TextEncoderStream() {
        if (!(this instanceof TextEncoderStream)) return new TextEncoderStream();
        var enc = new TextEncoder();
        TransformStream.call(this, {
            transform: function (chunk, controller) {
                controller.enqueue(enc.encode(String(chunk == null ? '' : chunk)));
            }
        });
        Object.defineProperty(this, 'encoding', { value: 'utf-8', configurable: true });
    }
    TextEncoderStream.prototype = Object.create(TransformStream.prototype);
    TextEncoderStream.prototype.constructor = TextEncoderStream;
    defineCtor('TextEncoderStream', TextEncoderStream);

    function TextDecoderStream(label, options) {
        if (!(this instanceof TextDecoderStream)) return new TextDecoderStream(label, options);
        var dec = new TextDecoder(label || 'utf-8', options);
        TransformStream.call(this, {
            transform: function (chunk, controller) {
                var out;
                try { out = dec.decode(chunk, { stream: true }); }
                catch (e) { out = String(chunk); }
                if (out) controller.enqueue(out);
            },
            flush: function (controller) {
                try {
                    var rest = dec.decode();
                    if (rest) controller.enqueue(rest);
                } catch (e) { /* ignore */ }
            }
        });
        Object.defineProperty(this, 'encoding', {
            value: label ? String(label).toLowerCase() : 'utf-8',
            configurable: true
        });
    }
    TextDecoderStream.prototype = Object.create(TransformStream.prototype);
    TextDecoderStream.prototype.constructor = TextDecoderStream;
    defineCtor('TextDecoderStream', TextDecoderStream);

    // Intl (ECMA-402) is implemented natively in C; see src/js_intl.c.

    var zlibCreate = global.__ns_zlib_create;
    var zlibPush   = global.__ns_zlib_push;
    var zlibFinish = global.__ns_zlib_finish;
    try {
        delete global.__ns_zlib_create;
        delete global.__ns_zlib_push;
        delete global.__ns_zlib_finish;
    } catch (e) { /* ignore */ }

    var ZLIB_FORMATS = { 'gzip': 1, 'deflate': 1, 'deflate-raw': 1 };

    function zlibTransformer(format, decompress) {
        var fmt = String(format);
        if (!ZLIB_FORMATS[fmt])
            throw new TypeError("Unsupported compression format: '" + fmt + "'");
        if (typeof zlibCreate !== 'function') {
            return { transform: function (chunk, ctl) { ctl.enqueue(chunk); } };
        }
        var codec = zlibCreate(fmt, decompress);
        return {
            transform: function (chunk, ctl) {
                var u8 = chunk instanceof Uint8Array ? chunk
                       : ArrayBuffer.isView(chunk)
                         ? new Uint8Array(chunk.buffer, chunk.byteOffset, chunk.byteLength)
                         : new Uint8Array(chunk);
                var out = zlibPush(codec, u8);
                if (out && out.byteLength) ctl.enqueue(new Uint8Array(out));
            },
            flush: function (ctl) {
                var out = zlibFinish(codec);
                if (out && out.byteLength) ctl.enqueue(new Uint8Array(out));
            }
        };
    }

    function CompressionStream(format) {
        if (!(this instanceof CompressionStream)) return new CompressionStream(format);
        TransformStream.call(this, zlibTransformer(format, false));
    }
    CompressionStream.prototype = Object.create(TransformStream.prototype);
    CompressionStream.prototype.constructor = CompressionStream;
    defineCtor('CompressionStream', CompressionStream);

    function DecompressionStream(format) {
        if (!(this instanceof DecompressionStream)) return new DecompressionStream(format);
        TransformStream.call(this, zlibTransformer(format, true));
    }
    DecompressionStream.prototype = Object.create(TransformStream.prototype);
    DecompressionStream.prototype.constructor = DecompressionStream;
    defineCtor('DecompressionStream', DecompressionStream);

    if (typeof Request === 'function' && typeof Response === 'function') {
        var caches = new WeakMap();
        var cacheOf = idlBrand(caches);
        var cacheStorageOf;

        function cacheKey(request, ignoreSearch) {
            var url;
            if (request && typeof request === 'object' && request.url)
                url = request.url;
            else { try { url = new Request(request).url; } catch (e) { url = String(request); } }
            if (ignoreSearch) {
                var q = url.indexOf('?');
                if (q >= 0) url = url.slice(0, q);
            }
            return url;
        }

        function headerPairs(h) {
            var out = [];
            if (!h) return out;
            try {
                if (typeof h.forEach === 'function') {
                    h.forEach(function (v, k) { out.push([k, v]); });
                } else if (typeof h.entries === 'function') {
                    var it = h.entries(), e;
                    while (!(e = it.next()).done) out.push([e.value[0], e.value[1]]);
                }
            } catch (err) {}
            return out;
        }

        function requestMethod(request) {
            if (request && typeof request === 'object' && request.method)
                return String(request.method).toUpperCase();
            return 'GET';
        }

        function queryOptions(options, multiCache) {
            options = options === undefined || options === null ? {} : Object(options);
            var query = {
                ignoreSearch: !!options.ignoreSearch,
                ignoreMethod: !!options.ignoreMethod,
                cacheName: undefined
            };
            if (multiCache && options.cacheName !== undefined) query.cacheName = String(options.cacheName);
            return query;
        }

        function entryToResponse(entry) {
            var resp = new Response(new Uint8Array(entry.body), {
                status: entry.status,
                statusText: entry.statusText,
                headers: entry.headers
            });
            try { resp.url = entry.url; } catch (e) {}
            return resp;
        }

        function cacheMatches(entries, request, options) {
            if (request === undefined) return Array.from(entries.keys());
            if (!options.ignoreMethod && requestMethod(request) !== 'GET' &&
                requestMethod(request) !== 'HEAD')
                return [];
            var want = cacheKey(request, options.ignoreSearch);
            return Array.from(entries.keys()).filter(function (key) {
                if (!options.ignoreSearch) return key === want;
                var q = key.indexOf('?');
                return (q >= 0 ? key.slice(0, q) : key) === want;
            });
        }

        function cachePut(state, request, response) {
            if (requestMethod(request) !== 'GET')
                return Promise.reject(new TypeError("Failed to execute 'put' on 'Cache': Request method '" +
                                                    requestMethod(request) + "' is unsupported"));
            if (response === null || typeof response !== 'object' ||
                typeof response.arrayBuffer !== 'function')
                return Promise.reject(new TypeError("Failed to execute 'put' on 'Cache': parameter 2 is not of type 'Response'."));
            if (response.bodyUsed)
                return Promise.reject(new TypeError("Failed to execute 'put' on 'Cache': Response body is already used"));
            if (response.status === 206)
                return Promise.reject(new TypeError("Failed to execute 'put' on 'Cache': Partial response (status code 206) is unsupported"));
            var key = cacheKey(request);
            var src = typeof response.clone === 'function' ? response.clone() : response;
            return Promise.resolve(src.arrayBuffer()).then(function (ab) {
                state.entries.set(key, {
                    body: ab,
                    status: response.status === undefined ? 200 : response.status,
                    statusText: response.statusText || '',
                    headers: headerPairs(response.headers),
                    url: response.url || ''
                });
            });
        }

        function cacheAdd(state, request) {
            return fetch(request).then(function (resp) {
                if (!resp.ok)
                    throw new TypeError("Failed to execute 'add' on 'Cache': Request failed with status " + resp.status);
                return cachePut(state, request, resp);
            });
        }

        class Cache {
            constructor() { throw idlIllegalConstructor('Cache'); }
            match(request, options = {}) {
                return idlAsync(this, cacheOf, arguments, 1, 'Cache', 'match', function (state) {
                    var keys = cacheMatches(state.entries, request, queryOptions(options));
                    return keys.length ? entryToResponse(state.entries.get(keys[0])) : undefined;
                });
            }
            matchAll(request = undefined, options = {}) {
                return idlAsync(this, cacheOf, arguments, 0, 'Cache', 'matchAll', function (state) {
                    return cacheMatches(state.entries, request, queryOptions(options)).map(function (key) {
                        return entryToResponse(state.entries.get(key));
                    });
                });
            }
            add(request) {
                return idlAsync(this, cacheOf, arguments, 1, 'Cache', 'add', function (state) {
                    return cacheAdd(state, request);
                });
            }
            addAll(requests) {
                return idlAsync(this, cacheOf, arguments, 1, 'Cache', 'addAll', function (state) {
                    return Promise.all(Array.from(requests).map(function (request) {
                        return cacheAdd(state, request);
                    })).then(function () { return undefined; });
                });
            }
            put(request, response) {
                return idlAsync(this, cacheOf, arguments, 2, 'Cache', 'put', function (state) {
                    return cachePut(state, request, response);
                });
            }
            delete(request, options = {}) {
                return idlAsync(this, cacheOf, arguments, 1, 'Cache', 'delete', function (state) {
                    var keys = cacheMatches(state.entries, request, queryOptions(options));
                    keys.forEach(function (key) { state.entries.delete(key); });
                    return keys.length > 0;
                });
            }
            keys(request = undefined, options = {}) {
                return idlAsync(this, cacheOf, arguments, 0, 'Cache', 'keys', function (state) {
                    return cacheMatches(state.entries, request, queryOptions(options)).map(function (key) {
                        return new Request(key);
                    });
                });
            }
        }

        function newCache(entries) {
            var cache = Object.create(Cache.prototype);
            caches.set(cache, { entries: entries });
            return cache;
        }

        class CacheStorage {
            constructor() { throw idlIllegalConstructor('CacheStorage'); }
            match(request, options = {}) {
                return idlAsync(this, cacheStorageOf, arguments, 1, 'CacheStorage', 'match', function (state) {
                    var opts = queryOptions(options, true);
                    var names = opts.cacheName === undefined ? Array.from(state.stores.keys()) :
                        (state.stores.has(opts.cacheName) ? [opts.cacheName] : []);
                    for (var i = 0; i < names.length; i++) {
                        var entries = state.stores.get(names[i]);
                        var keys = cacheMatches(entries, request, opts);
                        if (keys.length) return entryToResponse(entries.get(keys[0]));
                    }
                    return undefined;
                });
            }
            has(cacheName) {
                return idlAsync(this, cacheStorageOf, arguments, 1, 'CacheStorage', 'has', function (state) {
                    return state.stores.has(String(cacheName));
                });
            }
            open(cacheName) {
                return idlAsync(this, cacheStorageOf, arguments, 1, 'CacheStorage', 'open', function (state) {
                    var name = String(cacheName);
                    var entries = state.stores.get(name);
                    if (!entries) {
                        entries = new Map();
                        state.stores.set(name, entries);
                    }
                    return newCache(entries);
                });
            }
            delete(cacheName) {
                return idlAsync(this, cacheStorageOf, arguments, 1, 'CacheStorage', 'delete', function (state) {
                    return state.stores.delete(String(cacheName));
                });
            }
            keys() {
                return idlAsync(this, cacheStorageOf, arguments, 0, 'CacheStorage', 'keys', function (state) {
                    return Array.from(state.stores.keys());
                });
            }
        }

        idlExpose(Cache, 'Cache', null);
        idlExpose(CacheStorage, 'CacheStorage', null);
        cacheStorageOf = idlSingletonBrand(CacheStorage.prototype, { stores: new Map() });
        try { global.caches = Object.create(CacheStorage.prototype); } catch (e) {}
    }

    if (typeof Object.hasOwn !== 'function') {
        Object.hasOwn = function (obj, prop) {
            if (obj == null) throw new TypeError('Object.hasOwn: null/undefined');
            return Object.prototype.hasOwnProperty.call(Object(obj), prop);
        };
    }

    if (typeof Object.fromEntries !== 'function') {
        Object.fromEntries = function (iter) {
            var out = {};
            if (iter == null) throw new TypeError('Object.fromEntries: null/undefined');
            var it = iter[Symbol.iterator] ? iter[Symbol.iterator]() : null;
            if (it) {
                for (var step = it.next(); !step.done; step = it.next()) {
                    var pair = step.value;
                    if (pair == null) throw new TypeError('Object.fromEntries: bad pair');
                    out[String(pair[0])] = pair[1];
                }
            } else if (typeof iter.length === 'number') {
                for (var i = 0; i < iter.length; i++) {
                    var p = iter[i];
                    if (p == null) continue;
                    out[String(p[0])] = p[1];
                }
            } else {
                var keys = Object.keys(iter);
                for (var k = 0; k < keys.length; k++) out[keys[k]] = iter[keys[k]];
            }
            return out;
        };
    }

    if (typeof Promise.withResolvers !== 'function') {
        Promise.withResolvers = function () {
            var resolve, reject;
            var promise = new Promise(function (res, rej) {
                resolve = res; reject = rej;
            });
            return { promise: promise, resolve: resolve, reject: reject };
        };
    }

    if (typeof Promise.any !== 'function') {
        Promise.any = function (iter) {
            var arr = [];
            var it = iter[Symbol.iterator] ? iter[Symbol.iterator]() : null;
            if (it) {
                for (var step = it.next(); !step.done; step = it.next()) arr.push(step.value);
            } else {
                for (var i = 0; i < iter.length; i++) arr.push(iter[i]);
            }
            return new Promise(function (resolve, reject) {
                if (arr.length === 0) {
                    var err = new Error('All promises were rejected');
                    err.name = 'AggregateError';
                    err.errors = [];
                    reject(err);
                    return;
                }
                var errors = new Array(arr.length);
                var remaining = arr.length;
                arr.forEach(function (p, idx) {
                    Promise.resolve(p).then(resolve, function (e) {
                        errors[idx] = e;
                        if (--remaining === 0) {
                            var aerr = new Error('All promises were rejected');
                            aerr.name = 'AggregateError';
                            aerr.errors = errors;
                            reject(aerr);
                        }
                    });
                });
            });
        };
    }

    if (typeof Promise.allSettled !== 'function') {
        Promise.allSettled = function (iter) {
            var arr = [];
            var it = iter[Symbol.iterator] ? iter[Symbol.iterator]() : null;
            if (it) {
                for (var step = it.next(); !step.done; step = it.next()) arr.push(step.value);
            } else {
                for (var i = 0; i < iter.length; i++) arr.push(iter[i]);
            }
            return Promise.all(arr.map(function (p) {
                return Promise.resolve(p).then(
                    function (v) { return { status: 'fulfilled', value: v }; },
                    function (e) { return { status: 'rejected',  reason: e }; }
                );
            }));
        };
    }

    if (typeof Array.prototype.findLast !== 'function') {
        defineMethod(Array.prototype, 'findLast', function (pred, thisArg) {
            for (var i = this.length - 1; i >= 0; i--)
                if (pred.call(thisArg, this[i], i, this)) return this[i];
            return undefined;
        });
    }
    if (typeof Array.prototype.findLastIndex !== 'function') {
        defineMethod(Array.prototype, 'findLastIndex', function (pred, thisArg) {
            for (var i = this.length - 1; i >= 0; i--)
                if (pred.call(thisArg, this[i], i, this)) return i;
            return -1;
        });
    }
    if (typeof Array.prototype.toSorted !== 'function') {
        defineMethod(Array.prototype, 'toSorted', function (cmp) {
            return this.slice().sort(cmp);
        });
    }
    if (typeof Array.prototype.toReversed !== 'function') {
        defineMethod(Array.prototype, 'toReversed', function () {
            return this.slice().reverse();
        });
    }
    if (typeof Array.prototype.toSpliced !== 'function') {
        defineMethod(Array.prototype, 'toSpliced', function (start, count) {
            var copy = this.slice();
            var args = Array.prototype.slice.call(arguments);
            copy.splice.apply(copy, args);
            return copy;
        });
    }
    if (typeof Array.prototype.with !== 'function') {
        defineMethod(Array.prototype, 'with', function (idx, value) {
            var len = this.length;
            if (idx < 0) idx += len;
            if (idx < 0 || idx >= len) throw new RangeError('with: index out of range');
            var copy = this.slice();
            copy[idx] = value;
            return copy;
        });
    }

    if (typeof Object.groupBy !== 'function') {
        Object.groupBy = function (iter, keyFn) {
            var out = Object.create(null);
            var idx = 0;
            var arr = iter && typeof iter.length === 'number' && typeof iter !== 'string'
                ? iter : Array.from(iter);
            for (var i = 0; i < arr.length; i++) {
                var key = keyFn(arr[i], idx++);
                if (!Object.prototype.hasOwnProperty.call(out, key)) out[key] = [];
                out[key].push(arr[i]);
            }
            return out;
        };
    }
    if (typeof Map !== 'undefined' && typeof Map.groupBy !== 'function') {
        Map.groupBy = function (iter, keyFn) {
            var m = new Map();
            var idx = 0;
            var arr = iter && typeof iter.length === 'number' && typeof iter !== 'string'
                ? iter : Array.from(iter);
            for (var i = 0; i < arr.length; i++) {
                var key = keyFn(arr[i], idx++);
                if (!m.has(key)) m.set(key, []);
                m.get(key).push(arr[i]);
            }
            return m;
        };
    }

    if (typeof String.prototype.replaceAll !== 'function') {
        defineMethod(String.prototype, 'replaceAll', function (search, replacement) {
            if (search instanceof RegExp) {
                if (!search.global)
                    throw new TypeError('replaceAll called with a non-global RegExp');
                return this.replace(search, replacement);
            }
            var s = String(this);
            var needle = String(search);
            if (needle === '') {
                if (typeof replacement === 'function') {
                    var out = '';
                    for (var i = 0; i <= s.length; i++) {
                        out += String(replacement('', i, s));
                        if (i < s.length) out += s.charAt(i);
                    }
                    return out;
                }
                return Array.prototype.join.call(s, replacement) + replacement;
            }
            var parts = s.split(needle);
            if (typeof replacement === 'function') {
                var pos = 0, idx = 0;
                var result = '';
                for (var j = 0; j < parts.length; j++) {
                    result += parts[j];
                    if (j < parts.length - 1) {
                        pos += parts[j].length;
                        result += String(replacement(needle, pos, s));
                        pos += needle.length;
                    }
                }
                return result;
            }
            return parts.join(String(replacement));
        });
    }

    if (typeof globalThis.structuredClone !== 'function') {
        defineCtor('structuredClone', function (value) {
            return structClone(value, new Map());
        });
    }
    function structClone(v, seen) {
        if (v == null || typeof v !== 'object') return v;
        if (seen.has(v)) return seen.get(v);
        if (v instanceof Date) return new Date(v.getTime());
        if (v instanceof RegExp) return new RegExp(v.source, v.flags);
        if (v instanceof ArrayBuffer) {
            var c = new ArrayBuffer(v.byteLength);
            new Uint8Array(c).set(new Uint8Array(v));
            return c;
        }
        if (ArrayBuffer.isView && ArrayBuffer.isView(v))
            return new v.constructor(v);
        if (v instanceof Map) {
            var nm = new Map(); seen.set(v, nm);
            v.forEach(function (val, key) {
                nm.set(structClone(key, seen), structClone(val, seen));
            });
            return nm;
        }
        if (v instanceof Set) {
            var ns = new Set(); seen.set(v, ns);
            v.forEach(function (val) { ns.add(structClone(val, seen)); });
            return ns;
        }
        if (Array.isArray(v)) {
            var na = new Array(v.length); seen.set(v, na);
            for (var i = 0; i < v.length; i++) na[i] = structClone(v[i], seen);
            return na;
        }
        var no = {};
        seen.set(v, no);
        for (var k in v) {
            if (Object.prototype.hasOwnProperty.call(v, k))
                no[k] = structClone(v[k], seen);
        }
        return no;
    }

    (function () {
        var backend = global.__nd_idb;
        if (!backend) return;
        if (global.indexedDB) {
            try { delete global.__nd_idb; } catch (e) {
                try { global.__nd_idb = undefined; } catch (e2) {}
            }
            return;
        }
        try { delete global.__nd_idb; } catch (e) {
            try { global.__nd_idb = undefined; } catch (e2) {}
        }

        var scheduleTask = global.setTimeout;
        var EventClass = global.Event;

        function ex(name, message) {
            try { return new DOMException(message || name, name); }
            catch (e) {
                var err = new Error(message || name);
                err.name = name;
                return err;
            }
        }

        function task(fn) { scheduleTask.call(global, fn, 0); }

        function parseStoredKeyPath(s) {
            try { return JSON.parse(s); }
            catch (e) { return null; }
        }

        function storedKeyPath(v) {
            return JSON.stringify(v === undefined ? null : v);
        }

        function isView(v) {
            return typeof ArrayBuffer !== 'undefined' &&
                ArrayBuffer.isView && ArrayBuffer.isView(v);
        }

        function bytesOf(v) {
            var u;
            if (v instanceof ArrayBuffer) u = new Uint8Array(v);
            else if (isView(v)) u = new Uint8Array(v.buffer, v.byteOffset, v.byteLength);
            else return null;
            var out = [];
            for (var i = 0; i < u.length; i++) out.push(u[i]);
            return out;
        }

        function canonKey(v, seen) {
            if (typeof v === 'number') {
                if (isNaN(v)) throw ex('DataError', 'Invalid IndexedDB key');
                if (!isFinite(v)) return { t: 'n', v: 0, inf: v > 0 ? 1 : -1 };
                return { t: 'n', v: v };
            }
            if (typeof v === 'string') return { t: 's', v: v };
            if (v instanceof Date) {
                var t = v.getTime();
                if (!isFinite(t)) throw ex('DataError', 'Invalid IndexedDB key');
                return { t: 'd', v: t };
            }
            var b = bytesOf(v);
            if (b) return { t: 'b', v: b };
            if (Array.isArray(v)) {
                if (seen.indexOf(v) >= 0) throw ex('DataError', 'Invalid IndexedDB key');
                seen.push(v);
                var a = [];
                for (var i = 0; i < v.length; i++) a.push(canonKey(v[i], seen));
                seen.pop();
                return { t: 'a', v: a };
            }
            throw ex('DataError', 'Invalid IndexedDB key');
        }

        function encodeKey(v) { return JSON.stringify(canonKey(v, [])); }

        function decodeCanon(c) {
            if (!c) return undefined;
            if (c.t === 'n') return c.inf ? c.inf * Infinity : c.v;
            if (c.t === 's') return c.v;
            if (c.t === 'd') return new Date(c.v);
            if (c.t === 'b') {
                var u = new Uint8Array(c.v.length);
                for (var i = 0; i < c.v.length; i++) u[i] = c.v[i];
                return u.buffer;
            }
            if (c.t === 'a') {
                var a = [];
                for (var j = 0; j < c.v.length; j++) a.push(decodeCanon(c.v[j]));
                return a;
            }
            return undefined;
        }

        function decodeKey(s) {
            return decodeCanon(JSON.parse(s));
        }

        function typeRank(t) {
            if (t === 'n') return 1;
            if (t === 'd') return 2;
            if (t === 's') return 3;
            if (t === 'b') return 4;
            if (t === 'a') return 5;
            return 0;
        }

        function cmpCanon(a, b) {
            var ra = typeRank(a.t), rb = typeRank(b.t);
            if (ra !== rb) return ra < rb ? -1 : 1;
            if (a.t === 'n') {
                var ai = a.inf || 0, bi = b.inf || 0;
                if (ai !== bi) return ai < bi ? -1 : 1;
            }
            if (a.t === 'n' || a.t === 'd') return a.v === b.v ? 0 : (a.v < b.v ? -1 : 1);
            if (a.t === 's') return a.v === b.v ? 0 : (a.v < b.v ? -1 : 1);
            if (a.t === 'b') {
                var n = Math.min(a.v.length, b.v.length);
                for (var i = 0; i < n; i++)
                    if (a.v[i] !== b.v[i]) return a.v[i] < b.v[i] ? -1 : 1;
                return a.v.length === b.v.length ? 0 : (a.v.length < b.v.length ? -1 : 1);
            }
            if (a.t === 'a') {
                var m = Math.min(a.v.length, b.v.length);
                for (var j = 0; j < m; j++) {
                    var c = cmpCanon(a.v[j], b.v[j]);
                    if (c) return c;
                }
                return a.v.length === b.v.length ? 0 : (a.v.length < b.v.length ? -1 : 1);
            }
            return 0;
        }

        function compareEncoded(a, b) {
            return cmpCanon(JSON.parse(a), JSON.parse(b));
        }

        function validKey(v) {
            try { encodeKey(v); return true; }
            catch (e) { return false; }
        }

        function unsafeKeyPathPart(p) {
            return p === '__proto__' || p === 'prototype' || p === 'constructor';
        }

        function keyPathGet(v, path) {
            if (path === null || path === undefined) return undefined;
            if (Array.isArray(path)) {
                var out = [];
                for (var i = 0; i < path.length; i++) out.push(keyPathGet(v, path[i]));
                return out;
            }
            if (path === '') return v;
            var cur = v;
            var parts = String(path).split('.');
            for (var j = 0; j < parts.length; j++) {
                if (unsafeKeyPathPart(parts[j])) return undefined;
                if (cur == null || !(parts[j] in Object(cur))) return undefined;
                cur = cur[parts[j]];
            }
            return cur;
        }

        function keyPathSet(v, path, key) {
            if (!path || Array.isArray(path)) return;
            var parts = String(path).split('.');
            for (var p = 0; p < parts.length; p++)
                if (unsafeKeyPathPart(parts[p])) return;
            var cur = v;
            for (var i = 0; i < parts.length - 1; i++) {
                if (cur[parts[i]] == null || typeof cur[parts[i]] !== 'object')
                    cur[parts[i]] = {};
                cur = cur[parts[i]];
            }
            cur[parts[parts.length - 1]] = key;
        }

        function sortedRecords(records, keyName, direction) {
            records = records || [];
            records.sort(function (a, b) {
                var c = compareEncoded(a[keyName], b[keyName]);
                if (!c && a.primaryKey && b.primaryKey)
                    c = compareEncoded(a.primaryKey, b.primaryKey);
                return c;
            });
            if (direction === 'nextunique' || direction === 'prevunique') {
                var out = [], last = null;
                for (var i = 0; i < records.length; i++) {
                    if (last !== records[i][keyName]) {
                        out.push(records[i]);
                        last = records[i][keyName];
                    }
                }
                records = out;
            }
            if (direction && direction.indexOf('prev') === 0) records.reverse();
            return records;
        }

        var requests = new WeakMap();
        var databases = new WeakMap();
        var transactions = new WeakMap();
        var objectStores = new WeakMap();
        var indexes = new WeakMap();
        var cursors = new WeakMap();
        var keyRanges = new WeakMap();
        var idbRecords = new WeakMap();
        var requestOf = idlBrand(requests);
        var databaseOf = idlBrand(databases);
        var transactionOf = idlBrand(transactions);
        var objectStoreOf = idlBrand(objectStores);
        var indexOf = idlBrand(indexes);
        var cursorOf = idlBrand(cursors);
        var keyRangeOf = idlBrand(keyRanges);
        var recordOf = idlBrand(idbRecords);
        var factoryOf;

        function inRangeEncoded(encoded, range) {
            if (!range) return true;
            var bounds = keyRanges.get(range);
            if (bounds.lowerEncoded !== null) {
                var cl = compareEncoded(encoded, bounds.lowerEncoded);
                if (cl < 0 || (cl === 0 && bounds.lowerOpen)) return false;
            }
            if (bounds.upperEncoded !== null) {
                var cu = compareEncoded(encoded, bounds.upperEncoded);
                if (cu > 0 || (cu === 0 && bounds.upperOpen)) return false;
            }
            return true;
        }

        function asRange(query) {
            if (query === undefined || query === null) return null;
            if (keyRanges.has(query)) return query;
            return newKeyRange(query, query, false, false);
        }

        function unsignedLong(value) {
            value = Number(value);
            return isFinite(value) ? Math.floor(Math.abs(value)) % 4294967296 : 0;
        }

        function oneOf(value, allowed, iface, member, attribute) {
            value = String(value);
            if (allowed.indexOf(value) >= 0) return value;
            throw new TypeError("Failed to execute '" + member + "' on '" + iface + "': The " +
                                attribute + " provided ('" + value + "') is not one of " +
                                allowed.map(function (v) { return "'" + v + "'"; }).join(', ') + '.');
        }

        function newRequest(proto, source, tx) {
            var req = Object.create(proto);
            requests.set(req, idlHandlerState({
                result: undefined, error: null, source: source || null,
                transaction: tx || null, readyState: 'pending'
            }));
            return req;
        }

        function bubbleEvent(event, target, parents) {
            idlPinTarget(event, target);
            return idlDispatchPath(event, [target].concat(parents));
        }

        function transactionParents(tx) {
            return tx ? [tx, transactions.get(tx).db] : [];
        }

        function succeed(req, result) {
            var s = requests.get(req);
            s.result = result;
            s.error = null;
            s.readyState = 'done';
            req.dispatchEvent(idlTrustedEvent(new EventClass('success')));
        }

        function fail(req, err) {
            var s = requests.get(req);
            s.result = undefined;
            s.error = err && err.name ? err : ex('UnknownError', String(err || 'IndexedDB error'));
            s.readyState = 'done';
            var event = idlTrustedEvent(new EventClass('error', { bubbles: true, cancelable: true }));
            return !bubbleEvent(event, req, transactionParents(s.transaction));
        }

        function newKeyRange(lower, upper, lowerOpen, upperOpen) {
            var range = Object.create(IDBKeyRange.prototype);
            keyRanges.set(range, {
                lower: lower, upper: upper,
                lowerOpen: !!lowerOpen, upperOpen: !!upperOpen,
                lowerEncoded: lower === undefined ? null : encodeKey(lower),
                upperEncoded: upper === undefined ? null : encodeKey(upper)
            });
            return range;
        }

        class IDBKeyRange {
            constructor() { throw idlIllegalConstructor('IDBKeyRange'); }
            static only(value) {
                idlNeed(arguments, 1, 'IDBKeyRange', 'only');
                return newKeyRange(value, value, false, false);
            }
            static lowerBound(lower, open = false) {
                idlNeed(arguments, 1, 'IDBKeyRange', 'lowerBound');
                return newKeyRange(lower, undefined, !!open, true);
            }
            static upperBound(upper, open = false) {
                idlNeed(arguments, 1, 'IDBKeyRange', 'upperBound');
                return newKeyRange(undefined, upper, true, !!open);
            }
            static bound(lower, upper, lowerOpen = false, upperOpen = false) {
                idlNeed(arguments, 2, 'IDBKeyRange', 'bound');
                var cmp = cmpCanon(canonKey(lower, []), canonKey(upper, []));
                if (cmp > 0 || (cmp === 0 && (lowerOpen || upperOpen)))
                    throw ex('DataError', 'The lower key is greater than the upper key');
                return newKeyRange(lower, upper, !!lowerOpen, !!upperOpen);
            }
            get lower() { return keyRangeOf(this).lower; }
            get upper() { return keyRangeOf(this).upper; }
            get lowerOpen() { return keyRangeOf(this).lowerOpen; }
            get upperOpen() { return keyRangeOf(this).upperOpen; }
            includes(key) {
                keyRangeOf(this);
                idlNeed(arguments, 1, 'IDBKeyRange', 'includes');
                return inRangeEncoded(encodeKey(key), this);
            }
        }

        class IDBRecord {
            constructor() { throw idlIllegalConstructor('IDBRecord'); }
            get key() { return recordOf(this).key; }
            get primaryKey() { return recordOf(this).primaryKey; }
            get value() { return recordOf(this).value; }
        }

        function newRecord(key, primaryKey, value) {
            var record = Object.create(IDBRecord.prototype);
            idbRecords.set(record, { key: key, primaryKey: primaryKey, value: value });
            return record;
        }

        class IDBRequest {
            constructor() { throw idlIllegalConstructor('IDBRequest'); }
            get result() {
                var s = requestOf(this);
                if (s.readyState !== 'done')
                    throw ex('InvalidStateError', "Failed to read the 'result' property from 'IDBRequest': The request has not finished.");
                return s.result;
            }
            get error() {
                var s = requestOf(this);
                if (s.readyState !== 'done')
                    throw ex('InvalidStateError', "Failed to read the 'error' property from 'IDBRequest': The request has not finished.");
                return s.error;
            }
            get source() { return requestOf(this).source; }
            get transaction() { return requestOf(this).transaction; }
            get readyState() { return requestOf(this).readyState; }
        }
        idlEventHandlers(IDBRequest.prototype, ['success', 'error'], requestOf);

        class IDBOpenDBRequest extends IDBRequest {
            constructor() { throw idlIllegalConstructor('IDBOpenDBRequest'); }
        }
        idlEventHandlers(IDBOpenDBRequest.prototype, ['blocked', 'upgradeneeded'], requestOf);

        class IDBVersionChangeEvent extends Event {
            constructor(type, eventInitDict = undefined) {
                idlNeedCtor(arguments, 1, 'IDBVersionChangeEvent');
                super(type, eventInitDict);
                var init = eventInitDict === undefined || eventInitDict === null ? {} : Object(eventInitDict);
                if (init.oldVersion !== undefined) this.oldVersion = Math.floor(Number(init.oldVersion)) || 0;
                if (init.newVersion !== undefined && init.newVersion !== null)
                    this.newVersion = Math.floor(Number(init.newVersion)) || 0;
            }
        }

        function newVersionChangeEvent(type, oldVersion, newVersion) {
            return idlTrustedEvent(new IDBVersionChangeEvent(type, { oldVersion: oldVersion, newVersion: newVersion }));
        }

        function loadStores(db, info) {
            var s = databases.get(db);
            var kept = {};
            var list = [];
            var stores = info && info.stores || [];
            for (var i = 0; i < stores.length; i++) {
                var src = stores[i];
                var meta = s.stores[src.name] || { indexes: {} };
                meta.name = src.name;
                meta.keyPath = parseStoredKeyPath(src.keyPath);
                meta.autoIncrement = !!src.autoIncrement;
                var indexMetas = {};
                var idxNames = [];
                var idxList = src.indexes || [];
                for (var j = 0; j < idxList.length; j++) {
                    var ix = idxList[j];
                    indexMetas[ix.name] = {
                        name: ix.name,
                        keyPath: parseStoredKeyPath(ix.keyPath),
                        unique: !!ix.unique,
                        multiEntry: !!ix.multiEntry
                    };
                    idxNames.push(ix.name);
                }
                meta.indexes = indexMetas;
                meta.indexNames = idxNames.sort();
                kept[src.name] = meta;
                list.push(src.name);
            }
            s.stores = kept;
            s.storeNames = list.sort();
        }

        function refreshDatabase(db) {
            var s = databases.get(db);
            var info = backend.info(s.name);
            s.version = info.version;
            loadStores(db, info);
        }

        function newDatabase(name, version, info) {
            var db = Object.create(IDBDatabase.prototype);
            databases.set(db, idlHandlerState({
                name: name, version: version, stores: {}, storeNames: [],
                closed: false, upgradeTx: null
            }));
            loadStores(db, info);
            return db;
        }

        class IDBDatabase {
            constructor() { throw idlIllegalConstructor('IDBDatabase'); }
            get name() { return databaseOf(this).name; }
            get version() { return databaseOf(this).version; }
            get objectStoreNames() { return newDOMStringList(databaseOf(this).storeNames); }
            close() { databaseOf(this).closed = true; }
            createObjectStore(name, options = {}) {
                var s = databaseOf(this);
                idlNeed(arguments, 1, 'IDBDatabase', 'createObjectStore');
                if (!s.upgradeTx)
                    throw ex('InvalidStateError', "Failed to execute 'createObjectStore' on 'IDBDatabase': The database is not running a version change transaction.");
                name = String(name);
                options = options === undefined || options === null ? {} : Object(options);
                var kp = options.keyPath === undefined || options.keyPath === null ? null : options.keyPath;
                if (kp !== null && typeof kp !== 'string' && !Array.isArray(kp)) kp = String(kp);
                var autoIncrement = !!options.autoIncrement;
                if (s.stores[name])
                    throw ex('ConstraintError', "Failed to execute 'createObjectStore' on 'IDBDatabase': An object store with the specified name already exists.");
                if (autoIncrement && (kp === '' || Array.isArray(kp)))
                    throw ex('InvalidAccessError', "Failed to execute 'createObjectStore' on 'IDBDatabase': The autoIncrement option was set but the keyPath option was empty or an array.");
                backend.createStore(s.name, name, storedKeyPath(kp), autoIncrement);
                refreshDatabase(this);
                var tx = transactions.get(s.upgradeTx);
                if (tx.scope.indexOf(name) < 0) tx.scope.push(name);
                return s.upgradeTx.objectStore(name);
            }
            deleteObjectStore(name) {
                var s = databaseOf(this);
                idlNeed(arguments, 1, 'IDBDatabase', 'deleteObjectStore');
                if (!s.upgradeTx)
                    throw ex('InvalidStateError', "Failed to execute 'deleteObjectStore' on 'IDBDatabase': The database is not running a version change transaction.");
                name = String(name);
                if (!s.stores[name])
                    throw ex('NotFoundError', "Failed to execute 'deleteObjectStore' on 'IDBDatabase': The specified object store was not found.");
                backend.deleteStore(s.name, name);
                refreshDatabase(this);
            }
            transaction(storeNames, mode = 'readonly', options = {}) {
                var s = databaseOf(this);
                idlNeed(arguments, 1, 'IDBDatabase', 'transaction');
                if (typeof storeNames === 'string') storeNames = [storeNames];
                else storeNames = Array.prototype.map.call(Array.from(storeNames), String);
                mode = oneOf(mode, ['readonly', 'readwrite', 'versionchange'], 'IDBDatabase', 'transaction', 'mode');
                options = options === undefined || options === null ? {} : Object(options);
                var durability = options.durability === undefined ? 'default' :
                    oneOf(options.durability, ['default', 'strict', 'relaxed'], 'IDBDatabase', 'transaction', 'durability');
                if (s.closed)
                    throw ex('InvalidStateError', "Failed to execute 'transaction' on 'IDBDatabase': The database connection is closing.");
                if (!storeNames.length)
                    throw ex('InvalidAccessError', "Failed to execute 'transaction' on 'IDBDatabase': The storeNames parameter is empty.");
                for (var i = 0; i < storeNames.length; i++)
                    if (!s.stores[storeNames[i]])
                        throw ex('NotFoundError', "Failed to execute 'transaction' on 'IDBDatabase': One of the specified object stores was not found.");
                if (mode === 'versionchange')
                    throw new TypeError("Failed to execute 'transaction' on 'IDBDatabase': The mode provided ('versionchange') is not one of 'readonly' or 'readwrite'.");
                var tx = newTransaction(this, storeNames, mode, durability);
                task(function () { maybeComplete(tx); });
                return tx;
            }
        }
        idlEventHandlers(IDBDatabase.prototype, ['abort', 'close', 'error', 'versionchange'], databaseOf);

        function newTransaction(db, scope, mode, durability) {
            var tx = Object.create(IDBTransaction.prototype);
            transactions.set(tx, idlHandlerState({
                db: db, mode: mode, durability: durability || 'default', error: null,
                scope: scope.slice(), pending: 0, done: false, aborted: false,
                completeQueued: false, handles: {}
            }));
            return tx;
        }

        function requestFrom(source, tx, op) {
            var req = newRequest(IDBRequest.prototype, source, tx);
            if (tx) queueRequest(tx, req, op);
            else task(function () { try { succeed(req, op()); } catch (e) { fail(req, e); } });
            return req;
        }

        function queueRequest(tx, req, op) {
            var s = transactions.get(tx);
            if (s.done || s.aborted) throw ex('TransactionInactiveError', 'The transaction has finished.');
            s.pending++;
            task(function () {
                if (s.aborted) {
                    fail(req, s.error || ex('AbortError', 'Transaction aborted'));
                    s.pending--;
                    maybeComplete(tx);
                    return;
                }
                var result;
                var failure = null;
                try { result = op(); } catch (e) { failure = e || ex('UnknownError', 'IndexedDB error'); }
                if (failure) {
                    var handled = fail(req, failure);
                    if (!handled) abortTransaction(tx, failure);
                } else {
                    succeed(req, result);
                }
                s.pending--;
                maybeComplete(tx);
            });
        }

        function maybeComplete(tx) {
            var s = transactions.get(tx);
            if (s.pending !== 0 || s.done || s.aborted || s.completeQueued) return;
            s.completeQueued = true;
            task(function () {
                s.completeQueued = false;
                if (s.pending || s.done || s.aborted) return;
                s.done = true;
                tx.dispatchEvent(idlTrustedEvent(new EventClass('complete')));
                if (s.afterComplete) s.afterComplete();
            });
        }

        function abortTransaction(tx, err) {
            var s = transactions.get(tx);
            if (s.done || s.aborted) return;
            s.aborted = true;
            s.error = err && err.name ? err : ex('AbortError', 'Transaction aborted');
            bubbleEvent(idlTrustedEvent(new EventClass('abort', { bubbles: true })), tx, [s.db]);
            if (s.afterAbort) s.afterAbort();
        }

        class IDBTransaction {
            constructor() { throw idlIllegalConstructor('IDBTransaction'); }
            get objectStoreNames() {
                var s = transactionOf(this);
                var names = s.mode === 'versionchange' ? databases.get(s.db).storeNames : s.scope;
                return newDOMStringList(names.slice().sort());
            }
            get mode() { return transactionOf(this).mode; }
            get durability() { return transactionOf(this).durability; }
            get db() { return transactionOf(this).db; }
            get error() { return transactionOf(this).error; }
            objectStore(name) {
                var s = transactionOf(this);
                idlNeed(arguments, 1, 'IDBTransaction', 'objectStore');
                name = String(name);
                var meta = databases.get(s.db).stores[name];
                if (s.done || s.aborted)
                    throw ex('InvalidStateError', "Failed to execute 'objectStore' on 'IDBTransaction': The transaction has finished.");
                if (s.scope.indexOf(name) < 0 || !meta)
                    throw ex('NotFoundError', "Failed to execute 'objectStore' on 'IDBTransaction': The specified object store was not found.");
                var handle = s.handles[name];
                if (!handle || objectStores.get(handle).meta !== meta) {
                    handle = newObjectStore(this, meta);
                    s.handles[name] = handle;
                }
                return handle;
            }
            abort() {
                var s = transactionOf(this);
                if (s.done || s.aborted)
                    throw ex('InvalidStateError', "Failed to execute 'abort' on 'IDBTransaction': The transaction has already completed or aborted.");
                abortTransaction(this, ex('AbortError', 'Transaction aborted'));
            }
            commit() {
                var s = transactionOf(this);
                if (s.done || s.aborted)
                    throw ex('InvalidStateError', "Failed to execute 'commit' on 'IDBTransaction': The transaction has already completed or aborted.");
                maybeComplete(this);
            }
        }
        idlEventHandlers(IDBTransaction.prototype, ['abort', 'complete', 'error'], transactionOf);

        function newObjectStore(tx, meta) {
            var store = Object.create(IDBObjectStore.prototype);
            objectStores.set(store, { tx: tx, meta: meta, handles: {} });
            return store;
        }

        function storeParts(store) {
            var s = objectStores.get(store);
            var tx = transactions.get(s.tx);
            return { s: s, tx: tx, db: databases.get(tx.db).name, name: s.meta.name, meta: s.meta };
        }

        function assertWritable(store, member) {
            var p = storeParts(store);
            if (p.tx.done || p.tx.aborted)
                throw ex('TransactionInactiveError', "Failed to execute '" + member + "' on 'IDBObjectStore': The transaction has finished.");
            if (p.tx.mode === 'readonly')
                throw ex('ReadOnlyError', "Failed to execute '" + member + "' on 'IDBObjectStore': The transaction is read-only.");
            return p;
        }

        function storeKeyFor(p, value, key) {
            var inline = p.meta.keyPath !== null && p.meta.keyPath !== undefined;
            if (inline && key !== undefined)
                throw ex('DataError', 'Inline key stores do not accept explicit keys');
            if (inline) key = keyPathGet(value, p.meta.keyPath);
            if (key === undefined) {
                if (!p.meta.autoIncrement) throw ex('DataError', 'A key is required');
                key = backend.nextKey(p.db, p.name);
                if (inline) keyPathSet(value, p.meta.keyPath, key);
            }
            var encoded = encodeKey(key);
            var numeric = typeof key === 'number' && isFinite(key) && key >= 1 ? Math.floor(key) : undefined;
            return { key: key, encoded: encoded, numeric: numeric };
        }

        function storeIndexEntries(p, value) {
            var out = [];
            for (var n in p.meta.indexes) {
                var ix = p.meta.indexes[n];
                var raw = keyPathGet(value, ix.keyPath);
                if (raw === undefined) continue;
                if (ix.multiEntry && Array.isArray(raw)) {
                    var seen = {};
                    for (var i = 0; i < raw.length; i++) {
                        if (!validKey(raw[i])) continue;
                        var ek = encodeKey(raw[i]);
                        if (!seen[ek]) {
                            out.push({ name: n, key: ek });
                            seen[ek] = true;
                        }
                    }
                } else if (validKey(raw)) {
                    out.push({ name: n, key: encodeKey(raw) });
                }
            }
            return out;
        }

        function storeCheckUnique(p, entries, primary) {
            for (var i = 0; i < entries.length; i++) {
                var ix = p.meta.indexes[entries[i].name];
                if (!ix || !ix.unique) continue;
                var rows = backend.indexRecords(p.db, p.name, ix.name);
                for (var j = 0; j < rows.length; j++)
                    if (rows[j].key === entries[i].key && rows[j].primaryKey !== primary)
                        throw ex('ConstraintError', 'Unique index constraint failed');
            }
        }

        function storeRecords(p, query, direction) {
            var range = asRange(query);
            var rows = backend.records(p.db, p.name);
            var out = [];
            for (var i = 0; i < rows.length; i++)
                if (!range || inRangeEncoded(rows[i].key, range)) out.push(rows[i]);
            return sortedRecords(out, 'key', direction || 'next');
        }

        function storePut(store, value, key, addOnly, member) {
            var p = assertWritable(store, member);
            return requestFrom(store, p.s.tx, function () {
                var k = storeKeyFor(p, value, key);
                var entries = storeIndexEntries(p, value);
                storeCheckUnique(p, entries, k.encoded);
                backend.put(p.db, p.name, k.encoded, value, !!addOnly, entries, k.numeric);
                return k.key;
            });
        }

        function storeDelete(store, range) {
            var p = assertWritable(store, 'delete');
            return requestFrom(store, p.s.tx, function () {
                var r = storeRecords(p, range, 'next');
                for (var i = 0; i < r.length; i++)
                    backend.deleteRecord(p.db, p.name, r[i].key);
                return undefined;
            });
        }

        function checkedQuery(query) {
            if (query === undefined || query === null) return null;
            if (keyRanges.has(query)) return query;
            return asRange(query);
        }

        function limitedCount(count) {
            return count === undefined ? undefined : unsignedLong(count) || undefined;
        }

        function getAllArguments(queryOrOptions, count, iface, member) {
            var isOptions = queryOrOptions !== null && typeof queryOrOptions === 'object' &&
                !keyRanges.has(queryOrOptions) && !validKey(queryOrOptions);
            var options = isOptions ? queryOrOptions : { query: queryOrOptions, count: count };
            return {
                range: checkedQuery(options.query),
                limit: limitedCount(options.count),
                direction: options.direction === undefined || !isOptions ? 'next' :
                    validDirection(options.direction, iface, member)
            };
        }

        function validDirection(direction, iface, member) {
            return oneOf(direction, ['next', 'nextunique', 'prev', 'prevunique'], iface, member, 'direction');
        }

        function requireQuery(args, iface, member, query) {
            idlNeed(args, 1, iface, member);
            if (query === undefined || query === null)
                throw ex('DataError', "Failed to execute '" + member + "' on '" + iface + "': The parameter is not a valid key.");
            return asRange(query);
        }

        class IDBObjectStore {
            constructor() { throw idlIllegalConstructor('IDBObjectStore'); }
            get name() { return objectStoreOf(this).meta.name; }
            set name(value) {
                var s = objectStoreOf(this);
                if (transactions.get(s.tx).mode !== 'versionchange')
                    throw ex('InvalidStateError', "Failed to set the 'name' property on 'IDBObjectStore': The database is not running a version change transaction.");
                value = String(value);
                if (value === s.meta.name) return;
                throw ex('NotSupportedError', "Failed to set the 'name' property on 'IDBObjectStore': Renaming is not supported.");
            }
            get keyPath() { return objectStoreOf(this).meta.keyPath; }
            get indexNames() { return newDOMStringList(objectStoreOf(this).meta.indexNames); }
            get transaction() { return objectStoreOf(this).tx; }
            get autoIncrement() { return objectStoreOf(this).meta.autoIncrement; }
            put(value, key = undefined) {
                objectStoreOf(this);
                idlNeed(arguments, 1, 'IDBObjectStore', 'put');
                return storePut(this, value, key, false, 'put');
            }
            add(value, key = undefined) {
                objectStoreOf(this);
                idlNeed(arguments, 1, 'IDBObjectStore', 'add');
                return storePut(this, value, key, true, 'add');
            }
            delete(query) {
                objectStoreOf(this);
                idlNeed(arguments, 1, 'IDBObjectStore', 'delete');
                assertWritable(this, 'delete');
                return storeDelete(this, requireQuery(arguments, 'IDBObjectStore', 'delete', query));
            }
            clear() {
                objectStoreOf(this);
                var p = assertWritable(this, 'clear');
                return requestFrom(this, p.s.tx, function () {
                    backend.clear(p.db, p.name);
                    return undefined;
                });
            }
            get(query) {
                objectStoreOf(this);
                var range = requireQuery(arguments, 'IDBObjectStore', 'get', query);
                var p = storeParts(this);
                return requestFrom(this, p.s.tx, function () {
                    var bounds = keyRanges.get(range);
                    if (bounds.lowerEncoded !== null && bounds.lowerEncoded === bounds.upperEncoded &&
                        !bounds.lowerOpen && !bounds.upperOpen)
                        return backend.get(p.db, p.name, bounds.lowerEncoded);
                    var r = storeRecords(p, range, 'next');
                    return r.length ? r[0].value : undefined;
                });
            }
            getKey(query) {
                objectStoreOf(this);
                var range = requireQuery(arguments, 'IDBObjectStore', 'getKey', query);
                var p = storeParts(this);
                return requestFrom(this, p.s.tx, function () {
                    var r = storeRecords(p, range, 'next');
                    return r.length ? decodeKey(r[0].key) : undefined;
                });
            }
            getAll(queryOrOptions = undefined, count = undefined) {
                objectStoreOf(this);
                var a = getAllArguments(queryOrOptions, count, 'IDBObjectStore', 'getAll');
                var p = storeParts(this);
                return requestFrom(this, p.s.tx, function () {
                    var r = storeRecords(p, a.range, a.direction);
                    if (a.limit !== undefined) r = r.slice(0, a.limit);
                    return r.map(function (x) { return x.value; });
                });
            }
            getAllKeys(queryOrOptions = undefined, count = undefined) {
                objectStoreOf(this);
                var a = getAllArguments(queryOrOptions, count, 'IDBObjectStore', 'getAllKeys');
                var p = storeParts(this);
                return requestFrom(this, p.s.tx, function () {
                    var r = storeRecords(p, a.range, a.direction);
                    if (a.limit !== undefined) r = r.slice(0, a.limit);
                    return r.map(function (x) { return decodeKey(x.key); });
                });
            }
            getAllRecords(options = {}) {
                objectStoreOf(this);
                var a = getAllArguments(options === undefined || options === null ? {} : Object(options),
                                        undefined, 'IDBObjectStore', 'getAllRecords');
                var p = storeParts(this);
                return requestFrom(this, p.s.tx, function () {
                    var r = storeRecords(p, a.range, a.direction);
                    if (a.limit !== undefined) r = r.slice(0, a.limit);
                    return r.map(function (x) {
                        var k = decodeKey(x.key);
                        return newRecord(k, k, x.value);
                    });
                });
            }
            count(query = undefined) {
                objectStoreOf(this);
                var range = checkedQuery(query);
                var p = storeParts(this);
                return requestFrom(this, p.s.tx, function () {
                    return storeRecords(p, range, 'next').length;
                });
            }
            openCursor(query = undefined, direction = 'next') {
                objectStoreOf(this);
                var range = checkedQuery(query);
                direction = validDirection(direction, 'IDBObjectStore', 'openCursor');
                return cursorRequest(this, range, false, direction);
            }
            openKeyCursor(query = undefined, direction = 'next') {
                objectStoreOf(this);
                var range = checkedQuery(query);
                direction = validDirection(direction, 'IDBObjectStore', 'openKeyCursor');
                return cursorRequest(this, range, true, direction);
            }
            index(name) {
                var s = objectStoreOf(this);
                idlNeed(arguments, 1, 'IDBObjectStore', 'index');
                name = String(name);
                var meta = s.meta.indexes[name];
                if (!meta)
                    throw ex('NotFoundError', "Failed to execute 'index' on 'IDBObjectStore': The specified index was not found.");
                var handle = s.handles[name];
                if (!handle || indexes.get(handle).meta !== meta) {
                    handle = newIndex(this, meta);
                    s.handles[name] = handle;
                }
                return handle;
            }
            createIndex(name, keyPath, options = {}) {
                objectStoreOf(this);
                idlNeed(arguments, 2, 'IDBObjectStore', 'createIndex');
                var p = storeParts(this);
                if (p.tx.mode !== 'versionchange')
                    throw ex('InvalidStateError', "Failed to execute 'createIndex' on 'IDBObjectStore': The database is not running a version change transaction.");
                name = String(name);
                options = options === undefined || options === null ? {} : Object(options);
                if (p.meta.indexes[name])
                    throw ex('ConstraintError', "Failed to execute 'createIndex' on 'IDBObjectStore': An index with the specified name already exists.");
                if (typeof keyPath !== 'string' && !Array.isArray(keyPath)) keyPath = String(keyPath);
                if (!!options.multiEntry && Array.isArray(keyPath))
                    throw ex('InvalidAccessError', "Failed to execute 'createIndex' on 'IDBObjectStore': The keyPath argument was an array and the multiEntry option is true.");
                backend.createIndex(p.db, p.name, name, storedKeyPath(keyPath),
                                    !!options.unique, !!options.multiEntry);
                refreshDatabase(p.tx.db);
                var fresh = storeParts(this);
                var rows = backend.records(p.db, p.name);
                for (var i = 0; i < rows.length; i++) {
                    var entries = storeIndexEntries(fresh, rows[i].value);
                    backend.put(p.db, p.name, rows[i].key, rows[i].value, false, entries, undefined);
                }
                return this.index(name);
            }
            deleteIndex(name) {
                objectStoreOf(this);
                idlNeed(arguments, 1, 'IDBObjectStore', 'deleteIndex');
                var p = storeParts(this);
                if (p.tx.mode !== 'versionchange')
                    throw ex('InvalidStateError', "Failed to execute 'deleteIndex' on 'IDBObjectStore': The database is not running a version change transaction.");
                name = String(name);
                if (!p.meta.indexes[name])
                    throw ex('NotFoundError', "Failed to execute 'deleteIndex' on 'IDBObjectStore': The specified index was not found.");
                backend.deleteIndex(p.db, p.name, name);
                refreshDatabase(p.tx.db);
            }
        }

        function newIndex(store, meta) {
            var index = Object.create(IDBIndex.prototype);
            indexes.set(index, { store: store, meta: meta });
            return index;
        }

        function indexParts(index) {
            var s = indexes.get(index);
            var sp = storeParts(s.store);
            return { s: s, tx: sp.s.tx, db: sp.db, store: sp.name, meta: s.meta };
        }

        function indexRecords(p, query, direction) {
            var range = asRange(query);
            var rows = backend.indexRecords(p.db, p.store, p.meta.name);
            var out = [];
            for (var i = 0; i < rows.length; i++)
                if (!range || inRangeEncoded(rows[i].key, range)) out.push(rows[i]);
            return sortedRecords(out, 'key', direction || 'next');
        }

        class IDBIndex {
            constructor() { throw idlIllegalConstructor('IDBIndex'); }
            get name() { return indexOf(this).meta.name; }
            set name(value) {
                var s = indexOf(this);
                var tx = transactions.get(objectStores.get(s.store).tx);
                if (tx.mode !== 'versionchange')
                    throw ex('InvalidStateError', "Failed to set the 'name' property on 'IDBIndex': The database is not running a version change transaction.");
                value = String(value);
                if (value === s.meta.name) return;
                throw ex('NotSupportedError', "Failed to set the 'name' property on 'IDBIndex': Renaming is not supported.");
            }
            get objectStore() { return indexOf(this).store; }
            get keyPath() { return indexOf(this).meta.keyPath; }
            get multiEntry() { return indexOf(this).meta.multiEntry; }
            get unique() { return indexOf(this).meta.unique; }
            get(query) {
                indexOf(this);
                var range = requireQuery(arguments, 'IDBIndex', 'get', query);
                var p = indexParts(this);
                return requestFrom(this, p.tx, function () {
                    var r = indexRecords(p, range, 'next');
                    return r.length ? r[0].value : undefined;
                });
            }
            getKey(query) {
                indexOf(this);
                var range = requireQuery(arguments, 'IDBIndex', 'getKey', query);
                var p = indexParts(this);
                return requestFrom(this, p.tx, function () {
                    var r = indexRecords(p, range, 'next');
                    return r.length ? decodeKey(r[0].primaryKey) : undefined;
                });
            }
            getAll(queryOrOptions = undefined, count = undefined) {
                indexOf(this);
                var a = getAllArguments(queryOrOptions, count, 'IDBIndex', 'getAll');
                var p = indexParts(this);
                return requestFrom(this, p.tx, function () {
                    var r = indexRecords(p, a.range, a.direction);
                    if (a.limit !== undefined) r = r.slice(0, a.limit);
                    return r.map(function (x) { return x.value; });
                });
            }
            getAllKeys(queryOrOptions = undefined, count = undefined) {
                indexOf(this);
                var a = getAllArguments(queryOrOptions, count, 'IDBIndex', 'getAllKeys');
                var p = indexParts(this);
                return requestFrom(this, p.tx, function () {
                    var r = indexRecords(p, a.range, a.direction);
                    if (a.limit !== undefined) r = r.slice(0, a.limit);
                    return r.map(function (x) { return decodeKey(x.primaryKey); });
                });
            }
            getAllRecords(options = {}) {
                indexOf(this);
                var a = getAllArguments(options === undefined || options === null ? {} : Object(options),
                                        undefined, 'IDBIndex', 'getAllRecords');
                var p = indexParts(this);
                return requestFrom(this, p.tx, function () {
                    var r = indexRecords(p, a.range, a.direction);
                    if (a.limit !== undefined) r = r.slice(0, a.limit);
                    return r.map(function (x) {
                        return newRecord(decodeKey(x.key), decodeKey(x.primaryKey), x.value);
                    });
                });
            }
            count(query = undefined) {
                indexOf(this);
                var range = checkedQuery(query);
                var p = indexParts(this);
                return requestFrom(this, p.tx, function () {
                    return indexRecords(p, range, 'next').length;
                });
            }
            openCursor(query = undefined, direction = 'next') {
                indexOf(this);
                var range = checkedQuery(query);
                direction = validDirection(direction, 'IDBIndex', 'openCursor');
                return cursorRequest(this, range, false, direction);
            }
            openKeyCursor(query = undefined, direction = 'next') {
                indexOf(this);
                var range = checkedQuery(query);
                direction = validDirection(direction, 'IDBIndex', 'openKeyCursor');
                return cursorRequest(this, range, true, direction);
            }
        }

        function cursorSourceParts(source) {
            if (indexes.has(source)) {
                var ip = indexParts(source);
                return { tx: ip.tx, store: indexes.get(source).store, records: function (range, direction) {
                    return indexRecords(ip, range, direction);
                } };
            }
            var sp = storeParts(source);
            return { tx: sp.s.tx, store: source, records: function (range, direction) {
                return storeRecords(sp, range, direction);
            } };
        }

        function cursorRequest(source, range, keyOnly, direction) {
            var parts = cursorSourceParts(source);
            var req = newRequest(IDBRequest.prototype, source, parts.tx);
            queueRequest(parts.tx, req, function () {
                var records = parts.records(range, direction);
                if (!records.length) return null;
                return newCursor(source, records, keyOnly, direction, req);
            });
            return req;
        }

        function newCursor(source, records, keyOnly, direction, request) {
            var cursor = Object.create(keyOnly ? IDBCursor.prototype : IDBCursorWithValue.prototype);
            var s = {
                source: source, direction: direction || 'next', request: request,
                records: records, keyOnly: !!keyOnly, pos: 0,
                key: undefined, primaryKey: undefined, value: undefined
            };
            cursors.set(cursor, s);
            applyCursorPosition(s);
            return cursor;
        }

        function applyCursorPosition(s) {
            var r = s.records[s.pos];
            if (!r) return false;
            s.key = decodeKey(r.key);
            s.primaryKey = decodeKey(r.primaryKey || r.key);
            if (!s.keyOnly) s.value = r.value;
            return true;
        }

        function scheduleCursor(cursor) {
            var s = cursors.get(cursor);
            var req = s.request;
            var tx = requests.get(req).transaction;
            var ts = tx && transactions.get(tx);
            requests.get(req).readyState = 'pending';
            if (ts) ts.pending++;
            task(function () {
                try {
                    if (ts && ts.aborted)
                        fail(req, ts.error || ex('AbortError', 'Transaction aborted'));
                    else
                        succeed(req, applyCursorPosition(s) ? cursor : null);
                } finally {
                    if (ts) {
                        ts.pending--;
                        maybeComplete(tx);
                    }
                }
            });
        }

        function cursorMustBeActive(s, member) {
            var req = requests.get(s.request);
            var ts = req.transaction && transactions.get(req.transaction);
            if (ts && (ts.done || ts.aborted))
                throw ex('TransactionInactiveError', "Failed to execute '" + member + "' on 'IDBCursor': The transaction has finished.");
            if (req.readyState !== 'done' || !s.records[s.pos])
                throw ex('InvalidStateError', "Failed to execute '" + member + "' on 'IDBCursor': The cursor is being iterated or has iterated past its end.");
        }

        function cursorIsAscending(s) {
            return s.direction.indexOf('prev') !== 0;
        }

        class IDBCursor {
            constructor() { throw idlIllegalConstructor('IDBCursor'); }
            get source() { return cursorOf(this).source; }
            get direction() { return cursorOf(this).direction; }
            get key() { return cursorOf(this).key; }
            get primaryKey() { return cursorOf(this).primaryKey; }
            get request() { return cursorOf(this).request; }
            advance(count) {
                var s = cursorOf(this);
                idlNeed(arguments, 1, 'IDBCursor', 'advance');
                count = Number(count);
                if (!isFinite(count) || count < 0 || count > 4294967295)
                    throw new TypeError("Failed to execute 'advance' on 'IDBCursor': Value is outside the 'unsigned long' value range.");
                count = Math.floor(count);
                if (!count) throw new TypeError("Failed to execute 'advance' on 'IDBCursor': A count argument with value 0 (zero) was supplied, must be greater than 0.");
                cursorMustBeActive(s, 'advance');
                s.pos += count;
                scheduleCursor(this);
            }
            continue(key = undefined) {
                var s = cursorOf(this);
                var target = key === undefined ? null : encodeKey(key);
                cursorMustBeActive(s, 'continue');
                var ascending = cursorIsAscending(s);
                if (target !== null) {
                    var cmp = compareEncoded(target, s.records[s.pos].key);
                    if (ascending ? cmp <= 0 : cmp >= 0)
                        throw ex('DataError', "Failed to execute 'continue' on 'IDBCursor': The parameter is less than or equal to this cursor's position.");
                    while (s.pos < s.records.length &&
                           (ascending ? compareEncoded(s.records[s.pos].key, target) < 0
                                      : compareEncoded(s.records[s.pos].key, target) > 0))
                        s.pos++;
                } else {
                    s.pos++;
                }
                scheduleCursor(this);
            }
            continuePrimaryKey(key, primaryKey) {
                var s = cursorOf(this);
                idlNeed(arguments, 2, 'IDBCursor', 'continuePrimaryKey');
                if (!indexes.has(s.source))
                    throw ex('InvalidAccessError', "Failed to execute 'continuePrimaryKey' on 'IDBCursor': The cursor's source is not an index.");
                if (s.direction !== 'next' && s.direction !== 'prev')
                    throw ex('InvalidAccessError', "Failed to execute 'continuePrimaryKey' on 'IDBCursor': The cursor's direction is not 'next' or 'prev'.");
                var target = encodeKey(key);
                var primary = encodeKey(primaryKey);
                cursorMustBeActive(s, 'continuePrimaryKey');
                var ascending = cursorIsAscending(s);
                while (s.pos < s.records.length) {
                    s.pos++;
                    var r = s.records[s.pos];
                    if (!r) break;
                    var byKey = compareEncoded(r.key, target);
                    var byPrimary = compareEncoded(r.primaryKey || r.key, primary);
                    var passed = ascending ? (byKey > 0 || (byKey === 0 && byPrimary >= 0))
                                           : (byKey < 0 || (byKey === 0 && byPrimary <= 0));
                    if (passed) break;
                }
                scheduleCursor(this);
            }
            update(value) {
                var s = cursorOf(this);
                idlNeed(arguments, 1, 'IDBCursor', 'update');
                cursorMustBeActive(s, 'update');
                if (s.keyOnly)
                    throw ex('InvalidStateError', "Failed to execute 'update' on 'IDBCursor': The cursor is a key cursor.");
                var store = indexes.has(s.source) ? indexes.get(s.source).store : s.source;
                var p = assertWritable(store, 'update');
                var inline = p.meta.keyPath !== null && p.meta.keyPath !== undefined;
                var primary = s.primaryKey;
                if (inline && cmpCanon(canonKey(keyPathGet(value, p.meta.keyPath), []), canonKey(primary, [])) !== 0)
                    throw ex('DataError', "Failed to execute 'update' on 'IDBCursor': The effective key of the provided value differs from the cursor's primary key.");
                return storePut(store, value, inline ? undefined : primary, false, 'update');
            }
            delete() {
                var s = cursorOf(this);
                cursorMustBeActive(s, 'delete');
                if (s.keyOnly)
                    throw ex('InvalidStateError', "Failed to execute 'delete' on 'IDBCursor': The cursor is a key cursor.");
                var store = indexes.has(s.source) ? indexes.get(s.source).store : s.source;
                return storeDelete(store, asRange(s.primaryKey));
            }
        }

        class IDBCursorWithValue extends IDBCursor {
            constructor() { throw idlIllegalConstructor('IDBCursorWithValue'); }
            get value() { return cursorOf(this).value; }
        }

        class IDBFactory {
            constructor() { throw idlIllegalConstructor('IDBFactory'); }
            cmp(first, second) {
                factoryOf(this);
                idlNeed(arguments, 2, 'IDBFactory', 'cmp');
                return cmpCanon(canonKey(first, []), canonKey(second, []));
            }
            open(name, version = undefined) {
                factoryOf(this);
                idlNeed(arguments, 1, 'IDBFactory', 'open');
                name = String(name);
                if (version !== undefined) {
                    var wanted = Number(version);
                    if (!isFinite(wanted) || wanted < 0 || wanted > 9007199254740991)
                        throw new TypeError("Failed to execute 'open' on 'IDBFactory': Value is outside the 'unsigned long long' value range.");
                    version = Math.floor(wanted);
                    if (version === 0)
                        throw new TypeError("Failed to execute 'open' on 'IDBFactory': The version provided must not be 0.");
                }
                var req = newRequest(IDBOpenDBRequest.prototype, null, null);
                task(function () { openDatabase(req, name, version); });
                return req;
            }
            deleteDatabase(name) {
                factoryOf(this);
                idlNeed(arguments, 1, 'IDBFactory', 'deleteDatabase');
                name = String(name);
                var req = newRequest(IDBOpenDBRequest.prototype, null, null);
                task(function () {
                    try {
                        backend.deleteDatabase(name);
                        succeed(req, undefined);
                    } catch (e) {
                        fail(req, e);
                    }
                });
                return req;
            }
            databases() {
                try { factoryOf(this); } catch (e) { return Promise.reject(e); }
                return new Promise(function (resolve, reject) {
                    task(function () {
                        try { resolve(backend.databases()); }
                        catch (e) { reject(e); }
                    });
                });
            }
        }

        function openDatabase(req, name, version) {
            try {
                var info = backend.open(name);
                var oldVersion = Number(info.version || 0);
                var wanted = version === undefined ? (oldVersion || 1) : version;
                if (wanted < oldVersion)
                    throw ex('VersionError', 'The requested version (' + wanted + ') is less than the existing version (' + oldVersion + ').');
                var db = newDatabase(name, oldVersion || wanted, info);
                if (wanted > oldVersion) upgradeDatabase(req, db, oldVersion, wanted);
                else succeed(req, db);
            } catch (e) {
                fail(req, e);
            }
        }

        function upgradeDatabase(req, db, oldVersion, wanted) {
            var rs = requests.get(req);
            var ds = databases.get(db);
            var tx = newTransaction(db, ds.storeNames, 'versionchange', 'default');
            var ts = transactions.get(tx);
            ds.upgradeTx = tx;
            ts.afterComplete = function () {
                ds.upgradeTx = null;
                rs.transaction = null;
                succeed(req, db);
            };
            ts.afterAbort = function () {
                task(function () {
                    ds.upgradeTx = null;
                    ds.closed = true;
                    rs.transaction = null;
                    fail(req, ex('AbortError', 'The upgrade transaction was aborted.'));
                });
            };
            rs.result = db;
            rs.transaction = tx;
            rs.readyState = 'done';
            req.dispatchEvent(newVersionChangeEvent('upgradeneeded', oldVersion, wanted));
            if (ts.aborted) return;
            backend.setVersion(ds.name, wanted);
            refreshDatabase(db);
            ds.version = wanted;
            maybeComplete(tx);
        }

        idlExpose(IDBKeyRange, 'IDBKeyRange', null);
        idlExpose(IDBRecord, 'IDBRecord', null);
        idlExpose(IDBRequest, 'IDBRequest', idlEventTarget());
        idlExpose(IDBOpenDBRequest, 'IDBOpenDBRequest', IDBRequest);
        idlExpose(IDBVersionChangeEvent, 'IDBVersionChangeEvent', global.Event);
        idlExpose(IDBDatabase, 'IDBDatabase', idlEventTarget());
        idlExpose(IDBTransaction, 'IDBTransaction', idlEventTarget());
        idlExpose(IDBObjectStore, 'IDBObjectStore', null);
        idlExpose(IDBIndex, 'IDBIndex', null);
        idlExpose(IDBCursor, 'IDBCursor', null);
        idlExpose(IDBCursorWithValue, 'IDBCursorWithValue', IDBCursor);
        idlExpose(IDBFactory, 'IDBFactory', null);

        factoryOf = idlSingletonBrand(IDBFactory.prototype, {});
        defineCtor('indexedDB', Object.create(IDBFactory.prototype));
    })();

    // Workers need DOMException too; QuickJS-ng has it built in, the
    // original QuickJS does not.
    if (typeof DOMException !== 'function') {
        var DOM_EXCEPTION_CODES = {
            IndexSizeError:               1,
            HierarchyRequestError:        3,
            WrongDocumentError:           4,
            InvalidCharacterError:        5,
            NoModificationAllowedError:   7,
            NotFoundError:                8,
            NotSupportedError:            9,
            InUseAttributeError:         10,
            InvalidStateError:           11,
            SyntaxError:                 12,
            InvalidModificationError:    13,
            NamespaceError:              14,
            InvalidAccessError:          15,
            SecurityError:               18,
            NetworkError:                19,
            AbortError:                  20,
            URLMismatchError:            21,
            QuotaExceededError:          22,
            TimeoutError:                23,
            InvalidNodeTypeError:        24,
            DataCloneError:              25
        };
        var DomException = function (message, name) {
            if (!(this instanceof DomException)) return new DomException(message, name);
            var err = new Error(String(message == null ? '' : message));
            err.name = String(name == null ? 'Error' : name);
            err.code = DOM_EXCEPTION_CODES[err.name] || 0;
            Object.setPrototypeOf(err, DomException.prototype);
            return err;
        };
        DomException.prototype = Object.create(Error.prototype);
        DomException.prototype.constructor = DomException;
        for (var domExName in DOM_EXCEPTION_CODES) {
            if (Object.prototype.hasOwnProperty.call(DOM_EXCEPTION_CODES, domExName)) {
                try {
                    Object.defineProperty(DomException, domExName + '_CODE', {
                        value: DOM_EXCEPTION_CODES[domExName],
                        writable: false, enumerable: true, configurable: false
                    });
                } catch (e) {}
            }
        }
        defineCtor('DOMException', DomException);
    }

    if (typeof global.Observable !== 'function' && typeof global.AbortController === 'function') {
        var AbortControllerCtor = global.AbortController;
        var AbortSignalCtor = global.AbortSignal;
        var observables = new WeakMap();
        var subscribers = new WeakMap();
        var observableOf = idlBrand(observables);
        var subscriberOf = idlBrand(subscribers);

        function reportException(error) {
            if (typeof global.reportError === 'function') {
                global.reportError(error);
                return;
            }
            setTimeout(function () { throw error; }, 0);
        }

        function requireCallback(value, iface, member, position) {
            if (typeof value === 'function') return value;
            throw new TypeError("Failed to execute '" + member + "' on '" + iface +
                                "': The callback provided as parameter " + position + ' is not a function.');
        }

        function newObservable(callback) {
            var observable = Object.create(Observable.prototype);
            observables.set(observable, { callback: callback, subscriber: null });
            return observable;
        }

        function isActive(subscriber) {
            return !subscribers.get(subscriber).controller.signal.aborted;
        }

        function closeSubscription(subscriber, reason) {
            var s = subscribers.get(subscriber);
            if (s.controller.signal.aborted) return;
            s.controller.abort(reason);
            var teardowns = s.teardowns;
            s.teardowns = [];
            for (var i = teardowns.length - 1; i >= 0; i--) {
                try { teardowns[i](); }
                catch (e) { reportException(e); }
            }
        }

        function subscriberNext(subscriber, value) {
            if (!isActive(subscriber)) return;
            var snapshot = subscribers.get(subscriber).observers.slice();
            for (var i = 0; i < snapshot.length; i++) {
                if (!isActive(subscriber)) return;
                if (!snapshot[i].removed) snapshot[i].observer.next(value);
            }
        }

        function finishObservers(subscriber) {
            var s = subscribers.get(subscriber);
            var snapshot = s.observers;
            s.observers = [];
            snapshot.forEach(function (entry) { entry.removed = true; });
            return snapshot;
        }

        function subscriberError(subscriber, error) {
            if (!isActive(subscriber)) {
                reportException(error);
                return;
            }
            var snapshot = finishObservers(subscriber);
            closeSubscription(subscriber, error);
            snapshot.forEach(function (entry) { entry.observer.error(error); });
        }

        function subscriberComplete(subscriber) {
            if (!isActive(subscriber)) return;
            var snapshot = finishObservers(subscriber);
            closeSubscription(subscriber);
            snapshot.forEach(function (entry) { entry.observer.complete(); });
        }

        function removeObserver(subscriber, entry, reason) {
            if (entry.removed) return;
            entry.removed = true;
            var s = subscribers.get(subscriber);
            var index = s.observers.indexOf(entry);
            if (index >= 0) s.observers.splice(index, 1);
            if (!s.observers.length) closeSubscription(subscriber, reason);
        }

        function subscribeTo(source, observer, signal) {
            var o = observables.get(source);
            var subscriber = o.subscriber;
            var fresh = !subscriber || !isActive(subscriber);
            if (fresh) {
                subscriber = Object.create(Subscriber.prototype);
                subscribers.set(subscriber, {
                    observers: [], controller: new AbortControllerCtor(), teardowns: []
                });
                o.subscriber = subscriber;
            }
            var entry = { observer: observer, removed: false };
            subscribers.get(subscriber).observers.push(entry);
            if (signal) {
                if (signal.aborted) {
                    removeObserver(subscriber, entry, signal.reason);
                } else {
                    signal.addEventListener('abort', function () {
                        removeObserver(subscriber, entry, signal.reason);
                    }, { once: true });
                }
            }
            if (!fresh) return;
            try { o.callback.call(undefined, subscriber); }
            catch (e) { subscriberError(subscriber, e); }
        }

        function observerCallback(source, name) {
            var callback = source[name];
            if (callback === undefined) return null;
            if (typeof callback !== 'function')
                throw new TypeError("Failed to execute 'subscribe' on 'Observable': The provided callback is not a function.");
            return callback;
        }

        function toInternalObserver(observer) {
            var next = null, error = null, complete = null;
            if (typeof observer === 'function') {
                next = observer;
            } else if (observer === undefined || observer === null || typeof observer === 'object') {
                var dict = observer === undefined || observer === null ? {} : observer;
                complete = observerCallback(dict, 'complete');
                error = observerCallback(dict, 'error');
                next = observerCallback(dict, 'next');
            } else {
                throw new TypeError("Failed to execute 'subscribe' on 'Observable': The provided value is not of type '(ObserverCallback or Observer)'.");
            }
            return {
                next: function (value) {
                    if (!next) return;
                    try { next(value); } catch (e) { reportException(e); }
                },
                error: function (value) {
                    if (!error) { reportException(value); return; }
                    try { error(value); } catch (e) { reportException(e); }
                },
                complete: function () {
                    if (!complete) return;
                    try { complete(); } catch (e) { reportException(e); }
                }
            };
        }

        function optionsSignal(options) {
            if (options === undefined || options === null) return null;
            if (typeof options !== 'object' && typeof options !== 'function')
                throw new TypeError("The provided value is not of type 'SubscribeOptions'.");
            var signal = options.signal;
            if (signal === undefined) return null;
            if (!(signal instanceof AbortSignalCtor))
                throw new TypeError("Failed to read the 'signal' property from 'SubscribeOptions': Failed to convert value to 'AbortSignal'.");
            return signal;
        }

        function forwarding(subscriber) {
            return {
                next: function (value) { subscriberNext(subscriber, value); },
                error: function (error) { subscriberError(subscriber, error); },
                complete: function () { subscriberComplete(subscriber); }
            };
        }

        function getMethod(value, key) {
            var method = value[key];
            if (method === undefined || method === null) return undefined;
            if (typeof method !== 'function')
                throw new TypeError('The value is not iterable or observable.');
            return method;
        }

        function isObject(value) {
            return value !== null && (typeof value === 'object' || typeof value === 'function');
        }

        function asyncFromSync(iterator) {
            return {
                next: function () {
                    var result = iterator.next();
                    if (!isObject(result)) throw new TypeError('The iterator result is not an object.');
                    return Promise.resolve(result.value).then(function (v) {
                        return { value: v, done: result.done };
                    });
                },
                return: function (reason) {
                    var ret = iterator.return;
                    return typeof ret === 'function' ? ret.call(iterator, reason) : undefined;
                }
            };
        }

        function fromAsyncIterable(value) {
            return newObservable(function (subscriber) {
                if (!isActive(subscriber)) return;
                var method = getMethod(value, Symbol.asyncIterator);
                var iterator;
                if (method) {
                    iterator = method.call(value);
                    if (!isObject(iterator)) throw new TypeError('The async iterator is not an object.');
                } else {
                    var syncMethod = getMethod(value, Symbol.iterator);
                    if (!syncMethod) throw new TypeError('The value is not iterable.');
                    var syncIterator = syncMethod.call(value);
                    if (!isObject(syncIterator)) throw new TypeError('The iterator is not an object.');
                    iterator = asyncFromSync(syncIterator);
                }
                if (!isActive(subscriber)) return;
                var next = null, nextFailure = null;
                try { next = iterator.next; }
                catch (e) { nextFailure = { error: e }; }
                var closed = false;
                subscriber.addTeardown(function () {
                    if (closed) return;
                    closed = true;
                    var ret = iterator.return;
                    if (typeof ret !== 'function') return;
                    try {
                        var result = ret.call(iterator, subscriber.signal.reason);
                        if (result && typeof result.then === 'function') result.then(null, reportException);
                    } catch (e) { reportException(e); }
                });
                function fail(e) {
                    closed = true;
                    subscriberError(subscriber, e);
                }
                function deliver(item) {
                    try {
                        if (!isObject(item)) throw new TypeError('The async iterator result is not an object.');
                        if (item.done) {
                            closed = true;
                            subscriberComplete(subscriber);
                            return;
                        }
                        subscriberNext(subscriber, item.value);
                    } catch (e) {
                        fail(e);
                        return;
                    }
                    step();
                }
                function step() {
                    if (!isActive(subscriber)) return;
                    var pending;
                    try {
                        if (nextFailure) throw nextFailure.error;
                        pending = Promise.resolve(next.call(iterator));
                    } catch (e) { pending = Promise.reject(e); }
                    pending.then(deliver, fail);
                }
                step();
            });
        }

        function fromIterable(value) {
            return newObservable(function (subscriber) {
                if (!isActive(subscriber)) return;
                var method = getMethod(value, Symbol.iterator);
                var iterator = method.call(value);
                if (!isObject(iterator)) throw new TypeError('The iterator is not an object.');
                if (!isActive(subscriber)) return;
                var next = iterator.next;
                var done = false;
                subscriber.addTeardown(function () {
                    if (done) return;
                    done = true;
                    var ret = iterator.return;
                    if (typeof ret === 'function') ret.call(iterator, subscriber.signal.reason);
                });
                while (isActive(subscriber)) {
                    var item = next.call(iterator);
                    if (!isObject(item)) throw new TypeError('The iterator result is not an object.');
                    if (item.done) {
                        done = true;
                        subscriberComplete(subscriber);
                        return;
                    }
                    subscriberNext(subscriber, item.value);
                }
            });
        }

        function fromPromise(promise) {
            return newObservable(function (subscriber) {
                promise.then(function (value) {
                    subscriberNext(subscriber, value);
                    subscriberComplete(subscriber);
                }, function (error) {
                    subscriberError(subscriber, error);
                });
            });
        }

        function toObservable(value) {
            if (observables.has(value)) return value;
            if (value === null || (typeof value !== 'object' && typeof value !== 'function'))
                throw new TypeError('The value cannot be converted to an Observable.');
            if (getMethod(value, Symbol.asyncIterator)) return fromAsyncIterable(value);
            if (getMethod(value, Symbol.iterator)) return fromIterable(value);
            if (value instanceof Promise) return fromPromise(value);
            throw new TypeError('The value cannot be converted to an Observable.');
        }

        function promiseOperator(source, options, run) {
            var outer;
            try { outer = optionsSignal(options); }
            catch (e) { return Promise.reject(e); }
            return new Promise(function (resolve, reject) {
                var controller = new AbortControllerCtor();
                var signal = outer ? AbortSignalCtor.any([controller.signal, outer]) : controller.signal;
                if (signal.aborted) {
                    reject(signal.reason);
                    return;
                }
                signal.addEventListener('abort', function () { reject(signal.reason); }, { once: true });
                var settle = {
                    resolve: function (value) { resolve(value); controller.abort(); },
                    reject: function (error) { reject(error); controller.abort(error); }
                };
                subscribeTo(source, run(resolve, reject, settle), signal);
            });
        }

        function operatorBody(source, subscriber, handlers) {
            var observer = forwarding(subscriber);
            Object.keys(handlers).forEach(function (key) { observer[key] = handlers[key]; });
            subscribeTo(source, observer, subscriber.signal);
        }

        function callUser(subscriber, fn) {
            try { return { value: fn.apply(undefined, Array.prototype.slice.call(arguments, 2)) }; }
            catch (e) { subscriberError(subscriber, e); return null; }
        }

        function amountOf(value) {
            value = Math.trunc(Number(value));
            if (!isFinite(value)) return 0;
            return value < 0 ? value + 18446744073709551616 : value;
        }

        class Subscriber {
            constructor() { throw idlIllegalConstructor('Subscriber'); }
            next(value) {
                subscriberOf(this);
                idlNeed(arguments, 1, 'Subscriber', 'next');
                subscriberNext(this, value);
            }
            error(error) {
                subscriberOf(this);
                idlNeed(arguments, 1, 'Subscriber', 'error');
                subscriberError(this, error);
            }
            complete() {
                subscriberOf(this);
                subscriberComplete(this);
            }
            addTeardown(teardown) {
                var s = subscriberOf(this);
                idlNeed(arguments, 1, 'Subscriber', 'addTeardown');
                requireCallback(teardown, 'Subscriber', 'addTeardown', 1);
                if (!isActive(this)) {
                    try { teardown(); }
                    catch (e) { reportException(e); }
                    return;
                }
                s.teardowns.push(teardown);
            }
            get active() { subscriberOf(this); return isActive(this); }
            get signal() { return subscriberOf(this).controller.signal; }
        }

        class Observable {
            constructor(callback) {
                idlNeedCtor(arguments, 1, 'Observable');
                if (typeof callback !== 'function')
                    throw new TypeError("Failed to construct 'Observable': The callback provided as parameter 1 is not a function.");
                observables.set(this, { callback: callback });
            }
            static from(value) {
                idlNeed(arguments, 1, 'Observable', 'from');
                return toObservable(value);
            }
            subscribe(observer = {}, options = {}) {
                observableOf(this);
                subscribeTo(this, toInternalObserver(observer), optionsSignal(options));
            }
            takeUntil(notifier) {
                observableOf(this);
                idlNeed(arguments, 1, 'Observable', 'takeUntil');
                var source = this;
                var notifierObservable = toObservable(notifier);
                return newObservable(function (subscriber) {
                    subscribeTo(notifierObservable, {
                        next: function () { subscriberComplete(subscriber); },
                        error: function () { subscriberComplete(subscriber); },
                        complete: function () {}
                    }, subscriber.signal);
                    if (!isActive(subscriber)) return;
                    subscribeTo(source, forwarding(subscriber), subscriber.signal);
                });
            }
            map(mapper) {
                observableOf(this);
                idlNeed(arguments, 1, 'Observable', 'map');
                requireCallback(mapper, 'Observable', 'map', 1);
                var source = this;
                return newObservable(function (subscriber) {
                    var index = 0;
                    operatorBody(source, subscriber, {
                        next: function (value) {
                            var r = callUser(subscriber, mapper, value, index++);
                            if (r) subscriberNext(subscriber, r.value);
                        }
                    });
                });
            }
            filter(predicate) {
                observableOf(this);
                idlNeed(arguments, 1, 'Observable', 'filter');
                requireCallback(predicate, 'Observable', 'filter', 1);
                var source = this;
                return newObservable(function (subscriber) {
                    var index = 0;
                    operatorBody(source, subscriber, {
                        next: function (value) {
                            var r = callUser(subscriber, predicate, value, index++);
                            if (r && r.value) subscriberNext(subscriber, value);
                        }
                    });
                });
            }
            take(amount) {
                observableOf(this);
                idlNeed(arguments, 1, 'Observable', 'take');
                var count = amountOf(amount);
                var source = this;
                return newObservable(function (subscriber) {
                    var remaining = count;
                    if (remaining === 0) {
                        subscriberComplete(subscriber);
                        return;
                    }
                    operatorBody(source, subscriber, {
                        next: function (value) {
                            remaining--;
                            subscriberNext(subscriber, value);
                            if (remaining === 0) subscriberComplete(subscriber);
                        }
                    });
                });
            }
            drop(amount) {
                observableOf(this);
                idlNeed(arguments, 1, 'Observable', 'drop');
                var count = amountOf(amount);
                var source = this;
                return newObservable(function (subscriber) {
                    var remaining = count;
                    operatorBody(source, subscriber, {
                        next: function (value) {
                            if (remaining > 0) remaining--;
                            else subscriberNext(subscriber, value);
                        }
                    });
                });
            }
            flatMap(mapper) {
                observableOf(this);
                idlNeed(arguments, 1, 'Observable', 'flatMap');
                requireCallback(mapper, 'Observable', 'flatMap', 1);
                var source = this;
                return newObservable(function (subscriber) {
                    var queue = [];
                    var innerActive = false;
                    var outerDone = false;
                    var index = 0;
                    function drain() {
                        if (innerActive || !isActive(subscriber)) return;
                        if (!queue.length) {
                            if (outerDone) subscriberComplete(subscriber);
                            return;
                        }
                        var inner = queue.shift();
                        innerActive = true;
                        subscribeTo(inner, {
                            next: function (value) { subscriberNext(subscriber, value); },
                            error: function (e) { subscriberError(subscriber, e); },
                            complete: function () { innerActive = false; drain(); }
                        }, subscriber.signal);
                    }
                    operatorBody(source, subscriber, {
                        next: function (value) {
                            var inner;
                            try { inner = toObservable(mapper(value, index++)); }
                            catch (e) { subscriberError(subscriber, e); return; }
                            queue.push(inner);
                            drain();
                        },
                        complete: function () {
                            outerDone = true;
                            drain();
                        }
                    });
                });
            }
            switchMap(mapper) {
                observableOf(this);
                idlNeed(arguments, 1, 'Observable', 'switchMap');
                requireCallback(mapper, 'Observable', 'switchMap', 1);
                var source = this;
                return newObservable(function (subscriber) {
                    var innerController = null;
                    var outerDone = false;
                    var innerDone = true;
                    var index = 0;
                    operatorBody(source, subscriber, {
                        next: function (value) {
                            var inner;
                            try { inner = toObservable(mapper(value, index++)); }
                            catch (e) { subscriberError(subscriber, e); return; }
                            if (innerController) innerController.abort();
                            innerController = new AbortControllerCtor();
                            innerDone = false;
                            subscribeTo(inner, {
                                next: function (v) { subscriberNext(subscriber, v); },
                                error: function (e) { subscriberError(subscriber, e); },
                                complete: function () {
                                    innerDone = true;
                                    if (outerDone) subscriberComplete(subscriber);
                                }
                            }, AbortSignalCtor.any([innerController.signal, subscriber.signal]));
                        },
                        complete: function () {
                            outerDone = true;
                            if (innerDone) subscriberComplete(subscriber);
                        }
                    });
                });
            }
            inspect(inspectorUnion = {}) {
                observableOf(this);
                var inspector = {};
                if (typeof inspectorUnion === 'function') {
                    inspector.next = inspectorUnion;
                } else if (inspectorUnion !== undefined && inspectorUnion !== null) {
                    if (typeof inspectorUnion !== 'object')
                        throw new TypeError("Failed to execute 'inspect' on 'Observable': The provided value is not of type '(ObserverCallback or ObservableInspector)'.");
                    ['abort', 'complete', 'error', 'next', 'subscribe'].forEach(function (name) {
                        var callback = inspectorUnion[name];
                        if (callback === undefined) return;
                        inspector[name] = requireCallback(callback, 'Observable', 'inspect', 1);
                    });
                }
                var source = this;
                return newObservable(function (subscriber) {
                    var finished = false;
                    if (inspector.subscribe && !callUser(subscriber, inspector.subscribe)) return;
                    if (inspector.abort) {
                        subscriber.signal.addEventListener('abort', function () {
                            if (finished) return;
                            try { inspector.abort(subscriber.signal.reason); }
                            catch (e) { reportException(e); }
                        }, { once: true });
                    }
                    operatorBody(source, subscriber, {
                        next: function (value) {
                            if (inspector.next && !callUser(subscriber, inspector.next, value)) return;
                            subscriberNext(subscriber, value);
                        },
                        error: function (error) {
                            finished = true;
                            if (inspector.error && !callUser(subscriber, inspector.error, error)) return;
                            subscriberError(subscriber, error);
                        },
                        complete: function () {
                            finished = true;
                            if (inspector.complete && !callUser(subscriber, inspector.complete)) return;
                            subscriberComplete(subscriber);
                        }
                    });
                });
            }
            catch(callback) {
                observableOf(this);
                idlNeed(arguments, 1, 'Observable', 'catch');
                requireCallback(callback, 'Observable', 'catch', 1);
                var source = this;
                return newObservable(function (subscriber) {
                    operatorBody(source, subscriber, {
                        error: function (error) {
                            var inner;
                            try { inner = toObservable(callback(error)); }
                            catch (e) { subscriberError(subscriber, e); return; }
                            subscribeTo(inner, forwarding(subscriber), subscriber.signal);
                        }
                    });
                });
            }
            finally(callback) {
                observableOf(this);
                idlNeed(arguments, 1, 'Observable', 'finally');
                requireCallback(callback, 'Observable', 'finally', 1);
                var source = this;
                return newObservable(function (subscriber) {
                    subscriber.addTeardown(callback);
                    subscribeTo(source, forwarding(subscriber), subscriber.signal);
                });
            }
            toArray(options = {}) {
                try { observableOf(this); } catch (e) { return Promise.reject(e); }
                return promiseOperator(this, options, function (resolve, reject) {
                    var values = [];
                    return {
                        next: function (value) { values.push(value); },
                        error: reject,
                        complete: function () { resolve(values); }
                    };
                });
            }
            forEach(callback, options = {}) {
                try {
                    observableOf(this);
                    idlNeed(arguments, 1, 'Observable', 'forEach');
                    requireCallback(callback, 'Observable', 'forEach', 1);
                } catch (e) { return Promise.reject(e); }
                return promiseOperator(this, options, function (resolve, reject, settle) {
                    var index = 0;
                    return {
                        next: function (value) {
                            try { callback(value, index++); }
                            catch (e) { settle.reject(e); }
                        },
                        error: reject,
                        complete: function () { resolve(undefined); }
                    };
                });
            }
            every(predicate, options = {}) {
                try {
                    observableOf(this);
                    idlNeed(arguments, 1, 'Observable', 'every');
                    requireCallback(predicate, 'Observable', 'every', 1);
                } catch (e) { return Promise.reject(e); }
                return promiseOperator(this, options, function (resolve, reject, settle) {
                    var index = 0;
                    return {
                        next: function (value) {
                            try { if (!predicate(value, index++)) settle.resolve(false); }
                            catch (e) { settle.reject(e); }
                        },
                        error: reject,
                        complete: function () { resolve(true); }
                    };
                });
            }
            first(options = {}) {
                try { observableOf(this); } catch (e) { return Promise.reject(e); }
                return promiseOperator(this, options, function (resolve, reject, settle) {
                    return {
                        next: function (value) { settle.resolve(value); },
                        error: reject,
                        complete: function () {
                            reject(new RangeError('No values in Observable'));
                        }
                    };
                });
            }
            last(options = {}) {
                try { observableOf(this); } catch (e) { return Promise.reject(e); }
                return promiseOperator(this, options, function (resolve, reject) {
                    var seen = false, last;
                    return {
                        next: function (value) { seen = true; last = value; },
                        error: reject,
                        complete: function () {
                            if (seen) resolve(last);
                            else reject(new RangeError('No values in Observable'));
                        }
                    };
                });
            }
            find(predicate, options = {}) {
                try {
                    observableOf(this);
                    idlNeed(arguments, 1, 'Observable', 'find');
                    requireCallback(predicate, 'Observable', 'find', 1);
                } catch (e) { return Promise.reject(e); }
                return promiseOperator(this, options, function (resolve, reject, settle) {
                    var index = 0;
                    return {
                        next: function (value) {
                            try { if (predicate(value, index++)) settle.resolve(value); }
                            catch (e) { settle.reject(e); }
                        },
                        error: reject,
                        complete: function () { resolve(undefined); }
                    };
                });
            }
            some(predicate, options = {}) {
                try {
                    observableOf(this);
                    idlNeed(arguments, 1, 'Observable', 'some');
                    requireCallback(predicate, 'Observable', 'some', 1);
                } catch (e) { return Promise.reject(e); }
                return promiseOperator(this, options, function (resolve, reject, settle) {
                    var index = 0;
                    return {
                        next: function (value) {
                            try { if (predicate(value, index++)) settle.resolve(true); }
                            catch (e) { settle.reject(e); }
                        },
                        error: reject,
                        complete: function () { resolve(false); }
                    };
                });
            }
            reduce(reducer, initialValue = undefined, options = {}) {
                var hasInitial = arguments.length >= 2;
                try {
                    observableOf(this);
                    idlNeed(arguments, 1, 'Observable', 'reduce');
                    requireCallback(reducer, 'Observable', 'reduce', 1);
                } catch (e) { return Promise.reject(e); }
                return promiseOperator(this, options, function (resolve, reject, settle) {
                    var hasValue = hasInitial, accumulator = initialValue, index = hasInitial ? 0 : 1;
                    return {
                        next: function (value) {
                            if (!hasValue) {
                                hasValue = true;
                                accumulator = value;
                                return;
                            }
                            try { accumulator = reducer(accumulator, value, index++); }
                            catch (e) { settle.reject(e); }
                        },
                        error: reject,
                        complete: function () {
                            if (hasValue) resolve(accumulator);
                            else reject(new TypeError('Reduce of an empty Observable with no initial value'));
                        }
                    };
                });
            }
        }

        idlExpose(Subscriber, 'Subscriber', null);
        idlExpose(Observable, 'Observable', null);
    }

    /* WHATWG Geometry: DOMRect, DOMPoint, DOMQuad and the 3D DOMMatrix with CSS
     * transform-list parsing, as classes whose state lives in WeakMaps the page
     * cannot reach. CSS-3D pages (PolyCSS, cssQuake) project vertices through
     * new DOMPoint(...).matrixTransform(new DOMMatrix(str)). */
    (function () {
        var isWindow = !ndWorkerScope;
        var rectState = new WeakMap(), pointState = new WeakMap();
        var matrixState = new WeakMap(), quadState = new WeakMap();

        function illegal() { throw new TypeError('Illegal invocation'); }
        function state(map, o) {
            var s = map.get(o);
            if (!s) illegal();
            return s;
        }
        function domException(message, name) {
            try { return new DOMException(message, name); }
            catch (e) {
                var err = new Error(message);
                err.name = name;
                return err;
            }
        }
        function num(v, dflt) { return v === undefined ? dflt : +v; }
        function dictionary(v, label) {
            if (v === undefined || v === null) return {};
            if (typeof v !== 'object' && typeof v !== 'function')
                throw new TypeError("The provided value is not of type '" + label + "'.");
            return v;
        }

        function rectInit(other) {
            var d = dictionary(other, 'DOMRectInit');
            return { x: num(d.x, 0), y: num(d.y, 0),
                     width: num(d.width, 0), height: num(d.height, 0) };
        }
        function pointInit(other) {
            var d = dictionary(other, 'DOMPointInit');
            return { x: num(d.x, 0), y: num(d.y, 0), z: num(d.z, 0), w: num(d.w, 1) };
        }

        function rectJSON(s) {
            return { x: s.x, y: s.y, width: s.width, height: s.height,
                     top: Math.min(s.y, s.y + s.height),
                     right: Math.max(s.x, s.x + s.width),
                     bottom: Math.max(s.y, s.y + s.height),
                     left: Math.min(s.x, s.x + s.width) };
        }

        class DOMRectReadOnly {
            constructor(x = 0, y = 0, width = 0, height = 0) {
                rectState.set(this, { x: +x, y: +y, width: +width, height: +height });
            }
            get x() { return state(rectState, this).x; }
            get y() { return state(rectState, this).y; }
            get width() { return state(rectState, this).width; }
            get height() { return state(rectState, this).height; }
            get top() { var s = state(rectState, this); return Math.min(s.y, s.y + s.height); }
            get right() { var s = state(rectState, this); return Math.max(s.x, s.x + s.width); }
            get bottom() { var s = state(rectState, this); return Math.max(s.y, s.y + s.height); }
            get left() { var s = state(rectState, this); return Math.min(s.x, s.x + s.width); }
            toJSON() { return rectJSON(state(rectState, this)); }
            static fromRect(other = {}) {
                var d = rectInit(other);
                return new DOMRectReadOnly(d.x, d.y, d.width, d.height);
            }
        }

        class DOMRect extends DOMRectReadOnly {
            constructor(...args) { super(...args); }
            get x() { return state(rectState, this).x; }
            set x(v) { state(rectState, this).x = +v; }
            get y() { return state(rectState, this).y; }
            set y(v) { state(rectState, this).y = +v; }
            get width() { return state(rectState, this).width; }
            set width(v) { state(rectState, this).width = +v; }
            get height() { return state(rectState, this).height; }
            set height(v) { state(rectState, this).height = +v; }
            static fromRect(other = {}) {
                var d = rectInit(other);
                return new DOMRect(d.x, d.y, d.width, d.height);
            }
        }

        class DOMPointReadOnly {
            constructor(x = 0, y = 0, z = 0, w = 1) {
                pointState.set(this, { x: +x, y: +y, z: +z, w: +w });
            }
            get x() { return state(pointState, this).x; }
            get y() { return state(pointState, this).y; }
            get z() { return state(pointState, this).z; }
            get w() { return state(pointState, this).w; }
            matrixTransform(matrix = {}) {
                var s = state(pointState, this);
                return transformPoint(matrixFromInit(matrix).m, s);
            }
            toJSON() {
                var s = state(pointState, this);
                return { x: s.x, y: s.y, z: s.z, w: s.w };
            }
            static fromPoint(other = {}) {
                var d = pointInit(other);
                return new DOMPointReadOnly(d.x, d.y, d.z, d.w);
            }
        }

        class DOMPoint extends DOMPointReadOnly {
            constructor(...args) { super(...args); }
            get x() { return state(pointState, this).x; }
            set x(v) { state(pointState, this).x = +v; }
            get y() { return state(pointState, this).y; }
            set y(v) { state(pointState, this).y = +v; }
            get z() { return state(pointState, this).z; }
            set z(v) { state(pointState, this).z = +v; }
            get w() { return state(pointState, this).w; }
            set w(v) { state(pointState, this).w = +v; }
            static fromPoint(other = {}) {
                var d = pointInit(other);
                return new DOMPoint(d.x, d.y, d.z, d.w);
            }
        }

        function quadPoint(init) {
            var d = pointInit(init);
            return new DOMPoint(d.x, d.y, d.z, d.w);
        }

        class DOMQuad {
            constructor(p1 = {}, p2 = {}, p3 = {}, p4 = {}) {
                quadState.set(this, [quadPoint(p1), quadPoint(p2), quadPoint(p3), quadPoint(p4)]);
            }
            get p1() { return state(quadState, this)[0]; }
            get p2() { return state(quadState, this)[1]; }
            get p3() { return state(quadState, this)[2]; }
            get p4() { return state(quadState, this)[3]; }
            getBounds() {
                var q = state(quadState, this);
                var xs = q.map(function (p) { return p.x; });
                var ys = q.map(function (p) { return p.y; });
                var left = Math.min.apply(null, xs), top = Math.min.apply(null, ys);
                return new DOMRect(left, top, Math.max.apply(null, xs) - left,
                                   Math.max.apply(null, ys) - top);
            }
            toJSON() {
                var q = state(quadState, this);
                return { p1: q[0], p2: q[1], p3: q[2], p4: q[3] };
            }
            static fromRect(other = {}) {
                var r = rectInit(other);
                return new DOMQuad({ x: r.x, y: r.y }, { x: r.x + r.width, y: r.y },
                                   { x: r.x + r.width, y: r.y + r.height },
                                   { x: r.x, y: r.y + r.height });
            }
            static fromQuad(other = {}) {
                var d = dictionary(other, 'DOMQuadInit');
                return new DOMQuad(d.p1, d.p2, d.p3, d.p4);
            }
        }

        function identity() {
            return [1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1];
        }

        function isIdentity(m) {
            var id = identity();
            for (var i = 0; i < 16; i++) if (m[i] !== id[i]) return false;
            return true;
        }

        function multiply(A, B) {
            var out = new Array(16);
            for (var c = 0; c < 4; c++) {
                for (var r = 0; r < 4; r++) {
                    out[c * 4 + r] =
                        A[r]      * B[c * 4]     +
                        A[4 + r]  * B[c * 4 + 1] +
                        A[8 + r]  * B[c * 4 + 2] +
                        A[12 + r] * B[c * 4 + 3];
                }
            }
            return out;
        }

        function transformPoint(m, p) {
            return new DOMPoint(
                m[0] * p.x + m[4] * p.y + m[8] * p.z + m[12] * p.w,
                m[1] * p.x + m[5] * p.y + m[9] * p.z + m[13] * p.w,
                m[2] * p.x + m[6] * p.y + m[10] * p.z + m[14] * p.w,
                m[3] * p.x + m[7] * p.y + m[11] * p.z + m[15] * p.w);
        }

        var NUMBER = '[+-]?(?:\\d+\\.?\\d*|\\.\\d+)(?:[eE][+-]?\\d+)?';
        var numberRe = new RegExp('^' + NUMBER + '$');
        var angleRe = new RegExp('^(' + NUMBER + ')(deg|rad|grad|turn)?$');
        var lengthRe = new RegExp('^(' + NUMBER + ')(px)?$');

        function parseAngle(tok) {
            var m = angleRe.exec(tok);
            if (!m) return null;
            var v = parseFloat(m[1]);
            switch (m[2]) {
            case 'rad':  return v * 180 / Math.PI;
            case 'grad': return v * 0.9;
            case 'turn': return v * 360;
            default:     return v;
            }
        }
        function parseLength(tok) {
            var m = lengthRe.exec(tok);
            return m ? parseFloat(m[1]) : null;
        }
        function parseNumber(tok) {
            return numberRe.test(tok) ? parseFloat(tok) : null;
        }

        function rotation(x, y, z, deg) {
            var len = Math.sqrt(x * x + y * y + z * z);
            if (len === 0) return { m: identity(), is2D: true };
            x /= len; y /= len; z /= len;
            var rad = deg * Math.PI / 180;
            var s = Math.sin(rad), c = Math.cos(rad), t = 1 - c;
            if (x === 0 && y === 0)
                return { m: [c, s * z, 0, 0, -s * z, c, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1], is2D: true };
            if (y === 0 && z === 0)
                return { m: [1, 0, 0, 0, 0, c, s * x, 0, 0, -s * x, c, 0, 0, 0, 0, 1], is2D: false };
            if (x === 0 && z === 0)
                return { m: [c, 0, -s * y, 0, 0, 1, 0, 0, s * y, 0, c, 0, 0, 0, 0, 1], is2D: false };
            return {
                m: [
                    t * x * x + c,     t * x * y + s * z, t * x * z - s * y, 0,
                    t * x * y - s * z, t * y * y + c,     t * y * z + s * x, 0,
                    t * x * z + s * y, t * y * z - s * x, t * z * z + c,     0,
                    0, 0, 0, 1
                ],
                is2D: x === 0 && y === 0
            };
        }

        function allOf(args, parse, count) {
            if (args.length !== count) return null;
            var out = [];
            for (var i = 0; i < count; i++) {
                var v = parse(args[i]);
                if (v === null) return null;
                out.push(v);
            }
            return out;
        }

        function transformFunction(fn, args) {
            var m = identity(), v;
            switch (fn) {
            case 'matrix':
                v = allOf(args, parseNumber, 6);
                if (!v) return null;
                m[0] = v[0]; m[1] = v[1]; m[4] = v[2]; m[5] = v[3]; m[12] = v[4]; m[13] = v[5];
                return { m: m, is2D: true };
            case 'matrix3d':
                v = allOf(args, parseNumber, 16);
                return v ? { m: v, is2D: false } : null;
            case 'translate':
                if (args.length < 1 || args.length > 2) return null;
                m[12] = parseLength(args[0]);
                m[13] = args.length === 2 ? parseLength(args[1]) : 0;
                return m[12] === null || m[13] === null ? null : { m: m, is2D: true };
            case 'translatex':
                return args.length === 1 && (m[12] = parseLength(args[0])) !== null ? { m: m, is2D: true } : null;
            case 'translatey':
                return args.length === 1 && (m[13] = parseLength(args[0])) !== null ? { m: m, is2D: true } : null;
            case 'translatez':
                return args.length === 1 && (m[14] = parseLength(args[0])) !== null ? { m: m, is2D: false } : null;
            case 'translate3d':
                v = allOf(args, parseLength, 3);
                if (!v) return null;
                m[12] = v[0]; m[13] = v[1]; m[14] = v[2];
                return { m: m, is2D: false };
            case 'scale':
                if (args.length < 1 || args.length > 2) return null;
                m[0] = parseNumber(args[0]);
                m[5] = args.length === 2 ? parseNumber(args[1]) : m[0];
                return m[0] === null || m[5] === null ? null : { m: m, is2D: true };
            case 'scalex':
                return args.length === 1 && (m[0] = parseNumber(args[0])) !== null ? { m: m, is2D: true } : null;
            case 'scaley':
                return args.length === 1 && (m[5] = parseNumber(args[0])) !== null ? { m: m, is2D: true } : null;
            case 'scalez':
                return args.length === 1 && (m[10] = parseNumber(args[0])) !== null ? { m: m, is2D: false } : null;
            case 'scale3d':
                v = allOf(args, parseNumber, 3);
                if (!v) return null;
                m[0] = v[0]; m[5] = v[1]; m[10] = v[2];
                return { m: m, is2D: false };
            case 'rotate':
            case 'rotatez':
                return args.length === 1 && (v = parseAngle(args[0])) !== null
                    ? { m: rotation(0, 0, 1, v).m, is2D: true } : null;
            case 'rotatex':
                return args.length === 1 && (v = parseAngle(args[0])) !== null
                    ? { m: rotation(1, 0, 0, v).m, is2D: false } : null;
            case 'rotatey':
                return args.length === 1 && (v = parseAngle(args[0])) !== null
                    ? { m: rotation(0, 1, 0, v).m, is2D: false } : null;
            case 'rotate3d':
                if (args.length !== 4) return null;
                var axis = allOf(args.slice(0, 3), parseNumber, 3), deg = parseAngle(args[3]);
                return axis && deg !== null ? rotation(axis[0], axis[1], axis[2], deg) : null;
            case 'skew':
                if (args.length < 1 || args.length > 2) return null;
                var sx = parseAngle(args[0]), sy = args.length === 2 ? parseAngle(args[1]) : 0;
                if (sx === null || sy === null) return null;
                m[4] = Math.tan(sx * Math.PI / 180);
                m[1] = Math.tan(sy * Math.PI / 180);
                return { m: m, is2D: true };
            case 'skewx':
                if (args.length !== 1 || (v = parseAngle(args[0])) === null) return null;
                m[4] = Math.tan(v * Math.PI / 180);
                return { m: m, is2D: true };
            case 'skewy':
                if (args.length !== 1 || (v = parseAngle(args[0])) === null) return null;
                m[1] = Math.tan(v * Math.PI / 180);
                return { m: m, is2D: true };
            case 'perspective':
                if (args.length !== 1 || (v = parseLength(args[0])) === null) return null;
                if (v > 0) m[11] = -1 / v;
                return { m: m, is2D: false };
            default:
                return null;
            }
        }

        function parseTransformList(str) {
            var s = String(str).trim();
            if (!s || s === 'none') return { m: identity(), is2D: true };
            var m = identity(), is2D = true, consumed = 0, match;
            var re = /([a-zA-Z0-9]+)\s*\(([^)]*)\)/g;
            while ((match = re.exec(s)) !== null) {
                if (/\S/.test(s.slice(consumed, match.index))) return null;
                consumed = re.lastIndex;
                var args = match[2].split(',').map(function (a) { return a.trim(); });
                if (args.length === 1 && args[0] === '') args = [];
                var step = transformFunction(match[1].toLowerCase(), args);
                if (!step) return null;
                m = multiply(m, step.m);
                if (!step.is2D) is2D = false;
            }
            if (consumed === 0 || /\S/.test(s.slice(consumed))) return null;
            return { m: m, is2D: is2D };
        }

        function invert(m) {
            var inv = new Array(16);
            inv[0] = m[5]*m[10]*m[15] - m[5]*m[11]*m[14] - m[9]*m[6]*m[15] +
                     m[9]*m[7]*m[14] + m[13]*m[6]*m[11] - m[13]*m[7]*m[10];
            inv[4] = -m[4]*m[10]*m[15] + m[4]*m[11]*m[14] + m[8]*m[6]*m[15] -
                     m[8]*m[7]*m[14] - m[12]*m[6]*m[11] + m[12]*m[7]*m[10];
            inv[8] = m[4]*m[9]*m[15] - m[4]*m[11]*m[13] - m[8]*m[5]*m[15] +
                     m[8]*m[7]*m[13] + m[12]*m[5]*m[11] - m[12]*m[7]*m[9];
            inv[12] = -m[4]*m[9]*m[14] + m[4]*m[10]*m[13] + m[8]*m[5]*m[14] -
                      m[8]*m[6]*m[13] - m[12]*m[5]*m[10] + m[12]*m[6]*m[9];
            inv[1] = -m[1]*m[10]*m[15] + m[1]*m[11]*m[14] + m[9]*m[2]*m[15] -
                     m[9]*m[3]*m[14] - m[13]*m[2]*m[11] + m[13]*m[3]*m[10];
            inv[5] = m[0]*m[10]*m[15] - m[0]*m[11]*m[14] - m[8]*m[2]*m[15] +
                     m[8]*m[3]*m[14] + m[12]*m[2]*m[11] - m[12]*m[3]*m[10];
            inv[9] = -m[0]*m[9]*m[15] + m[0]*m[11]*m[13] + m[8]*m[1]*m[15] -
                     m[8]*m[3]*m[13] - m[12]*m[1]*m[11] + m[12]*m[3]*m[9];
            inv[13] = m[0]*m[9]*m[14] - m[0]*m[10]*m[13] - m[8]*m[1]*m[14] +
                      m[8]*m[2]*m[13] + m[12]*m[1]*m[10] - m[12]*m[2]*m[9];
            inv[2] = m[1]*m[6]*m[15] - m[1]*m[7]*m[14] - m[5]*m[2]*m[15] +
                     m[5]*m[3]*m[14] + m[13]*m[2]*m[7] - m[13]*m[3]*m[6];
            inv[6] = -m[0]*m[6]*m[15] + m[0]*m[7]*m[14] + m[4]*m[2]*m[15] -
                     m[4]*m[3]*m[14] - m[12]*m[2]*m[7] + m[12]*m[3]*m[6];
            inv[10] = m[0]*m[5]*m[15] - m[0]*m[7]*m[13] - m[4]*m[1]*m[15] +
                      m[4]*m[3]*m[13] + m[12]*m[1]*m[7] - m[12]*m[3]*m[5];
            inv[14] = -m[0]*m[5]*m[14] + m[0]*m[6]*m[13] + m[4]*m[1]*m[14] -
                      m[4]*m[2]*m[13] - m[12]*m[1]*m[6] + m[12]*m[2]*m[5];
            inv[3] = -m[1]*m[6]*m[11] + m[1]*m[7]*m[10] + m[5]*m[2]*m[11] -
                     m[5]*m[3]*m[10] - m[9]*m[2]*m[7] + m[9]*m[3]*m[6];
            inv[7] = m[0]*m[6]*m[11] - m[0]*m[7]*m[10] - m[4]*m[2]*m[11] +
                     m[4]*m[3]*m[10] + m[8]*m[2]*m[7] - m[8]*m[3]*m[6];
            inv[11] = -m[0]*m[5]*m[11] + m[0]*m[7]*m[9] + m[4]*m[1]*m[11] -
                      m[4]*m[3]*m[9] - m[8]*m[1]*m[7] + m[8]*m[3]*m[5];
            inv[15] = m[0]*m[5]*m[10] - m[0]*m[6]*m[9] - m[4]*m[1]*m[10] +
                      m[4]*m[2]*m[9] + m[8]*m[1]*m[6] - m[8]*m[2]*m[5];
            var det = m[0]*inv[0] + m[1]*inv[4] + m[2]*inv[8] + m[3]*inv[12];
            if (det === 0 || !isFinite(det)) return null;
            for (var i = 0; i < 16; i++) inv[i] /= det;
            return inv;
        }

        var FIELDS = [
            'm11', 'm12', 'm13', 'm14', 'm21', 'm22', 'm23', 'm24',
            'm31', 'm32', 'm33', 'm34', 'm41', 'm42', 'm43', 'm44'
        ];
        var ALIASES = [['a', 0], ['b', 1], ['c', 4], ['d', 5], ['e', 12], ['f', 13]];
        var THREE_D = [2, 3, 6, 7, 8, 9, 11, 14];

        function matrixFromInit(init) {
            var d = dictionary(init, 'DOMMatrixInit');
            var m = identity(), i, present = {};
            for (i = 0; i < ALIASES.length; i++) {
                var alias = d[ALIASES[i][0]], field = d[FIELDS[ALIASES[i][1]]];
                if (alias !== undefined && field !== undefined &&
                    !(+alias === +field || (+alias !== +alias && +field !== +field)))
                    throw new TypeError("The '" + ALIASES[i][0] + "' and '" +
                        FIELDS[ALIASES[i][1]] + "' members must be equal.");
                if (alias !== undefined) m[ALIASES[i][1]] = +alias;
                else if (field !== undefined) m[ALIASES[i][1]] = +field;
            }
            var only3d = false;
            for (i = 0; i < 16; i++) {
                if (ALIASES.some(function (a) { return a[1] === i; })) continue;
                if (d[FIELDS[i]] !== undefined) {
                    m[i] = +d[FIELDS[i]];
                    if (m[i] !== (i === 10 || i === 15 ? 1 : 0)) only3d = true;
                }
            }
            if (d.is2D !== undefined && d.is2D && only3d)
                throw new TypeError("The 3D members must have their default values when is2D is true.");
            return { m: m, is2D: d.is2D === undefined ? !only3d : !!d.is2D };
        }

        function matrixFromSequence(init) {
            var a = Array.from(init).map(function (v) { return +v; });
            if (a.length === 6)
                return { m: [a[0], a[1], 0, 0, a[2], a[3], 0, 0, 0, 0, 1, 0, a[4], a[5], 0, 1], is2D: true };
            if (a.length === 16) return { m: a, is2D: false };
            throw new TypeError('The sequence must contain 6 elements for a 2D matrix or 16 elements for a 3D matrix.');
        }

        function parseString(str) {
            if (!isWindow)
                throw new TypeError('DOMMatrix cannot be created from a string in this context.');
            var parsed = parseTransformList(str);
            if (!parsed) throw domException(
                "Failed to parse '" + str + "' as a transform list.", 'SyntaxError');
            return parsed;
        }

        function matrixFromAny(init) {
            if (init === undefined) return { m: identity(), is2D: true };
            if (typeof init === 'object' && init !== null &&
                typeof init[Symbol.iterator] === 'function')
                return matrixFromSequence(init);
            return parseString(String(init));
        }

        function make(Ctor, parsed) {
            var out = new Ctor();
            matrixState.set(out, parsed);
            return out;
        }

        function setState(self, parsed) {
            var s = state(matrixState, self);
            s.m = parsed.m;
            s.is2D = parsed.is2D;
            return self;
        }

        function translated(s, tx, ty, tz) {
            var t = identity();
            t[12] = tx; t[13] = ty; t[14] = tz;
            return { m: multiply(s.m, t), is2D: s.is2D && tz === 0 };
        }

        function scaled(s, sx, sy, sz, ox, oy, oz) {
            var r = translated(s, ox, oy, oz);
            var k = identity();
            k[0] = sx; k[5] = sy; k[10] = sz;
            r = { m: multiply(r.m, k), is2D: r.is2D && sz === 1 };
            return translated(r, -ox, -oy, -oz);
        }

        function rotated(s, rx, ry, rz) {
            var m = s.m;
            if (rz !== 0) m = multiply(m, rotation(0, 0, 1, rz).m);
            if (ry !== 0) m = multiply(m, rotation(0, 1, 0, ry).m);
            if (rx !== 0) m = multiply(m, rotation(1, 0, 0, rx).m);
            return { m: m, is2D: s.is2D && rx === 0 && ry === 0 };
        }

        function rotationAngles(rotX, rotY, rotZ) {
            if (rotY === undefined && rotZ === undefined) { rotZ = rotX; rotX = 0; rotY = 0; }
            return [+(rotX || 0), +(rotY || 0), +(rotZ || 0)];
        }

        var operations = {
            translate: function (s, tx = 0, ty = 0, tz = 0) {
                return translated(s, +tx, +ty, +tz);
            },
            scale: function (s, sx = 1, sy, sz = 1, ox = 0, oy = 0, oz = 0) {
                return scaled(s, +sx, sy === undefined ? +sx : +sy, +sz, +ox, +oy, +oz);
            },
            scale3d: function (s, k = 1, ox = 0, oy = 0, oz = 0) {
                return scaled(s, +k, +k, +k, +ox, +oy, +oz);
            },
            rotate: function (s, rx, ry, rz) {
                var a = rotationAngles(rx, ry, rz);
                return rotated(s, a[0], a[1], a[2]);
            },
            rotateFromVector: function (s, x = 0, y = 0) {
                x = +x; y = +y;
                return rotated(s, 0, 0, x === 0 && y === 0 ? 0 : Math.atan2(y, x) * 180 / Math.PI);
            },
            rotateAxisAngle: function (s, x = 0, y = 0, z = 0, angle = 0) {
                var r = rotation(+x, +y, +z, +angle);
                return { m: multiply(s.m, r.m), is2D: s.is2D && r.is2D };
            },
            skewX: function (s, sx = 0) {
                var t = identity();
                t[4] = Math.tan(+sx * Math.PI / 180);
                return { m: multiply(s.m, t), is2D: s.is2D };
            },
            skewY: function (s, sy = 0) {
                var t = identity();
                t[1] = Math.tan(+sy * Math.PI / 180);
                return { m: multiply(s.m, t), is2D: s.is2D };
            },
            multiply: function (s, other = {}) {
                var o = matrixFromInit(other);
                return { m: multiply(s.m, o.m), is2D: s.is2D && o.is2D };
            },
            inverse: function (s) {
                var inv = invert(s.m);
                return inv ? { m: inv, is2D: s.is2D }
                           : { m: identity().map(function () { return NaN; }), is2D: false };
            },
            flipX: function (s) {
                var t = identity();
                t[0] = -1;
                return { m: multiply(s.m, t), is2D: s.is2D };
            },
            flipY: function (s) {
                var t = identity();
                t[5] = -1;
                return { m: multiply(s.m, t), is2D: s.is2D };
            }
        };

        class DOMMatrixReadOnly {
            constructor(init = undefined) {
                matrixState.set(this, matrixFromAny(init));
            }
            get is2D() { return state(matrixState, this).is2D; }
            get isIdentity() { return isIdentity(state(matrixState, this).m); }
            translate(...args) {
                return make(DOMMatrix, operations.translate(state(matrixState, this), ...args));
            }
            scale(...args) {
                return make(DOMMatrix, operations.scale(state(matrixState, this), ...args));
            }
            scaleNonUniform(sx = 1, sy = 1) {
                return make(DOMMatrix, operations.scale(state(matrixState, this), sx, sy, 1, 0, 0, 0));
            }
            scale3d(...args) {
                return make(DOMMatrix, operations.scale3d(state(matrixState, this), ...args));
            }
            rotate(...args) {
                return make(DOMMatrix, operations.rotate(state(matrixState, this), ...args));
            }
            rotateFromVector(...args) {
                return make(DOMMatrix, operations.rotateFromVector(state(matrixState, this), ...args));
            }
            rotateAxisAngle(...args) {
                return make(DOMMatrix, operations.rotateAxisAngle(state(matrixState, this), ...args));
            }
            skewX(...args) { return make(DOMMatrix, operations.skewX(state(matrixState, this), ...args)); }
            skewY(...args) { return make(DOMMatrix, operations.skewY(state(matrixState, this), ...args)); }
            multiply(other = {}) {
                return make(DOMMatrix, operations.multiply(state(matrixState, this), other));
            }
            flipX() { return make(DOMMatrix, operations.flipX(state(matrixState, this))); }
            flipY() { return make(DOMMatrix, operations.flipY(state(matrixState, this))); }
            inverse() { return make(DOMMatrix, operations.inverse(state(matrixState, this))); }
            transformPoint(point = {}) {
                return transformPoint(state(matrixState, this).m, pointInit(point));
            }
            toFloat32Array() { return new Float32Array(state(matrixState, this).m); }
            toFloat64Array() { return new Float64Array(state(matrixState, this).m); }
            toString() {
                var s = state(matrixState, this);
                if (!s.m.every(isFinite))
                    throw domException('The matrix cannot be serialized: it has a non-finite value.',
                                       'InvalidStateError');
                return s.is2D
                    ? 'matrix(' + [s.m[0], s.m[1], s.m[4], s.m[5], s.m[12], s.m[13]].join(', ') + ')'
                    : 'matrix3d(' + s.m.join(', ') + ')';
            }
            toJSON() {
                var s = state(matrixState, this), out = {}, i;
                ALIASES.forEach(function (a) { out[a[0]] = s.m[a[1]]; });
                for (i = 0; i < 16; i++) out[FIELDS[i]] = s.m[i];
                out.is2D = s.is2D;
                out.isIdentity = isIdentity(s.m);
                return out;
            }
            static fromMatrix(other = {}) {
                return make(DOMMatrixReadOnly, matrixFromInit(other));
            }
            static fromFloat32Array(array32) {
                return make(DOMMatrixReadOnly, typedMatrix(array32, Float32Array));
            }
            static fromFloat64Array(array64) {
                return make(DOMMatrixReadOnly, typedMatrix(array64, Float64Array));
            }
        }

        function typedMatrix(array, Typed) {
            if (!(array instanceof Typed))
                throw new TypeError("The provided value is not of type '" + Typed.name + "'.");
            return matrixFromSequence(array);
        }

        function fieldAccessor(index) {
            return {
                get: function () { return state(matrixState, this).m[index]; },
                set: function (v) {
                    var s = state(matrixState, this);
                    v = +v;
                    s.m[index] = v;
                    if (THREE_D.indexOf(index) >= 0 ? v !== 0 : (index === 10 || index === 15) && v !== 1)
                        s.is2D = false;
                }
            };
        }

        class DOMMatrix extends DOMMatrixReadOnly {
            constructor(...args) { super(...args); }
            multiplySelf(other = {}) {
                return setState(this, operations.multiply(state(matrixState, this), other));
            }
            preMultiplySelf(other = {}) {
                var s = state(matrixState, this), o = matrixFromInit(other);
                return setState(this, { m: multiply(o.m, s.m), is2D: s.is2D && o.is2D });
            }
            translateSelf(...args) {
                return setState(this, operations.translate(state(matrixState, this), ...args));
            }
            scaleSelf(...args) {
                return setState(this, operations.scale(state(matrixState, this), ...args));
            }
            scale3dSelf(...args) {
                return setState(this, operations.scale3d(state(matrixState, this), ...args));
            }
            rotateSelf(...args) {
                return setState(this, operations.rotate(state(matrixState, this), ...args));
            }
            rotateFromVectorSelf(...args) {
                return setState(this, operations.rotateFromVector(state(matrixState, this), ...args));
            }
            rotateAxisAngleSelf(...args) {
                return setState(this, operations.rotateAxisAngle(state(matrixState, this), ...args));
            }
            skewXSelf(...args) { return setState(this, operations.skewX(state(matrixState, this), ...args)); }
            skewYSelf(...args) { return setState(this, operations.skewY(state(matrixState, this), ...args)); }
            invertSelf() { return setState(this, operations.inverse(state(matrixState, this))); }
            setMatrixValue(transformList) {
                return setState(this, parseString(String(transformList)));
            }
            static fromMatrix(other = {}) {
                return make(DOMMatrix, matrixFromInit(other));
            }
            static fromFloat32Array(array32) {
                return make(DOMMatrix, typedMatrix(array32, Float32Array));
            }
            static fromFloat64Array(array64) {
                return make(DOMMatrix, typedMatrix(array64, Float64Array));
            }
        }

        function defineField(name, index) {
            Object.defineProperty(DOMMatrixReadOnly.prototype, name, {
                get: function () { return state(matrixState, this).m[index]; },
                enumerable: true, configurable: true
            });
            var accessor = fieldAccessor(index);
            accessor.enumerable = true;
            accessor.configurable = true;
            Object.defineProperty(DOMMatrix.prototype, name, accessor);
        }
        FIELDS.forEach(function (name, i) { defineField(name, i); });
        ALIASES.forEach(function (a) { defineField(a[0], a[1]); });

        [DOMRectReadOnly, DOMRect, DOMPointReadOnly, DOMPoint, DOMQuad,
         DOMMatrixReadOnly, DOMMatrix].forEach(function (Ctor) {
            [Ctor, Ctor.prototype].forEach(function (target) {
                Object.getOwnPropertyNames(target).forEach(function (key) {
                    var d = Object.getOwnPropertyDescriptor(target, key);
                    if (key === 'constructor' || key === 'prototype' || key === 'length' ||
                        key === 'name' || !d.configurable) return;
                    d.enumerable = true;
                    Object.defineProperty(target, key, d);
                });
            });
            Object.defineProperty(Ctor.prototype, Symbol.toStringTag,
                                  { value: Ctor.name, configurable: true });
            replaceCtor(Ctor.name, Ctor);
        });
        if (isWindow) {
            replaceCtor('WebKitCSSMatrix', DOMMatrix);
        } else {
            delete DOMMatrix.prototype.setMatrixValue;
            delete DOMMatrixReadOnly.prototype.toString;
        }
    })();

    if (ndWorkerScope) return;

    if (typeof Symbol !== 'undefined') {
        if (typeof Symbol.dispose === 'undefined') {
            try { Symbol.dispose = Symbol('Symbol.dispose'); } catch (e) {}
        }
        if (typeof Symbol.asyncDispose === 'undefined') {
            try { Symbol.asyncDispose = Symbol('Symbol.asyncDispose'); } catch (e) {}
        }
    }


    if (typeof QuotaExceededError !== 'function') {
        var QuotaErr = function (message, options) {
            if (!(this instanceof QuotaErr)) return new QuotaErr(message, options);
            var err = new Error(String(message == null ? '' : message));
            err.name = 'QuotaExceededError';
            err.code = 22;
            err.requested = options && options.requested != null ? options.requested : null;
            err.quota = options && options.quota != null ? options.quota : null;
            Object.setPrototypeOf(err, QuotaErr.prototype);
            return err;
        };
        QuotaErr.prototype = Object.create(
            typeof DOMException === 'function' ? DOMException.prototype : Error.prototype);
        QuotaErr.prototype.constructor = QuotaErr;
        defineCtor('QuotaExceededError', QuotaErr);
    }


    if (typeof Event !== 'undefined' && Event.prototype &&
        typeof Event.prototype.composedPath !== 'function') {
        defineMethod(Event.prototype, 'composedPath', function () {
            var path = [];
            var node = this.target || this.currentTarget;
            while (node) {
                path.push(node);
                node = node.parentNode || null;
            }
            return path;
        });
    }

    var navigator = global.navigator;
    if (navigator && !navigator.locks) {
        try {
            Object.defineProperty(navigator, 'locks', {
                configurable: true, enumerable: true,
                value: {
                    request: function (name, options, callback) {
                        if (typeof options === 'function') {
                            callback = options;
                            options = undefined;
                        }
                        var lock = { name: String(name), mode: (options && options.mode) || 'exclusive' };
                        if (typeof callback !== 'function')
                            return Promise.resolve();
                        try { return Promise.resolve(callback(lock)); }
                        catch (e) { return Promise.reject(e); }
                    },
                    query: function () {
                        return Promise.resolve({ held: [], pending: [] });
                    }
                }
            });
        } catch (e) {}
    }

    if (navigator && !navigator.xr) {
        try {
            Object.defineProperty(navigator, 'xr', {
                configurable: true, enumerable: true,
                value: {
                    isSessionSupported: function () { return Promise.resolve(false); },
                    requestSession: function () {
                        var err = new Error('WebXR sessions are not supported');
                        err.name = 'NotSupportedError';
                        return Promise.reject(err);
                    },
                    addEventListener: function () {},
                    removeEventListener: function () {},
                    dispatchEvent: function () { return false; }
                }
            });
        } catch (e) {}
    }

    if (navigator && typeof navigator.share !== 'function') {
        try {
            Object.defineProperty(navigator, 'share', {
                configurable: true, enumerable: true,
                value: nativeize(function share() {
                    var err = new Error('Web Share API not supported');
                    err.name = 'NotSupportedError';
                    return Promise.reject(err);
                })
            });
            Object.defineProperty(navigator, 'canShare', {
                configurable: true, enumerable: true,
                value: nativeize(function canShare() { return false; })
            });
        } catch (e) {}
    }

    if (navigator) {
        try {
            Object.defineProperty(navigator, 'canShare', {
                configurable: true, enumerable: true,
                value: nativeize(function canShare() { return false; })
            });
        } catch (e) {}
        try {
            Object.defineProperty(navigator, 'vibrate', {
                configurable: true, enumerable: true,
                value: nativeize(function vibrate(pattern) {
                    var list = Array.isArray(pattern) ? pattern : [pattern];
                    for (var i = 0; i < list.length; i++) {
                        var v = Number(list[i]);
                        if (!isFinite(v) || v < 0) return false;
                    }
                    return true;
                })
            });
        } catch (e) {}
        try {
            Object.defineProperty(navigator, 'getAutoplayPolicy', {
                configurable: true, enumerable: true,
                value: nativeize(function getAutoplayPolicy() { return 'allowed'; })
            });
        } catch (e) {}
        if (navigator.mediaDevices) {
            try {
                Object.defineProperty(navigator.mediaDevices, 'getSupportedConstraints', {
                    configurable: true, enumerable: true,
                    value: nativeize(function getSupportedConstraints() {
                        return {
                            width: true, height: true, aspectRatio: true,
                            frameRate: true, facingMode: true, resizeMode: true,
                            sampleRate: true, sampleSize: true, channelCount: true,
                            echoCancellation: true, noiseSuppression: true,
                            autoGainControl: true, deviceId: true, groupId: true
                        };
                    })
                });
            } catch (e) {}

            (function () {
                var md = navigator.mediaDevices;
                var pending = [];

                function makeError(name, msg) {
                    var e;
                    try { e = new DOMException(msg, name); }
                    catch (_) { e = new Error(msg); e.name = name; }
                    return e;
                }
                function rnd() { return Math.random().toString(36).slice(2); }

                function makeTrack(kind, label) {
                    var L = {};
                    var track = {
                        __proto__: globalThis.__ndMediaStreamTrack
                            ? globalThis.__ndMediaStreamTrack.prototype : null,
                        kind: kind,
                        id: 'track-' + kind + '-' + rnd(),
                        label: label || (kind === 'video' ? 'Camera' : 'Microphone'),
                        enabled: true, muted: false, readyState: 'live', contentHint: '',
                        onended: null, onmute: null, onunmute: null,
                        getSettings: function () {
                            return kind === 'video'
                                ? { deviceId: 'default', groupId: 'default',
                                    width: 640, height: 480, frameRate: 30, facingMode: 'user' }
                                : { deviceId: 'default', groupId: 'default',
                                    sampleRate: 48000, channelCount: 1 };
                        },
                        getCapabilities: function () { return {}; },
                        getConstraints: function () { return {}; },
                        applyConstraints: function () { return Promise.resolve(); },
                        clone: function () { return makeTrack(kind, label); },
                        addEventListener: function (t, f) {
                            if (typeof f === 'function') (L[t] = L[t] || []).push(f);
                        },
                        removeEventListener: function (t, f) {
                            var a = L[t]; if (!a) return;
                            var i = a.indexOf(f); if (i >= 0) a.splice(i, 1);
                        },
                        dispatchEvent: function (e) {
                            var a = e && L[e.type];
                            if (a) a.slice().forEach(function (fn) {
                                try { fn.call(track, e); } catch (_) {}
                            });
                            var h = e && track['on' + e.type];
                            if (typeof h === 'function') { try { h.call(track, e); } catch (_) {} }
                            return true;
                        },
                        stop: function () {
                            if (track.readyState === 'ended') return;
                            track.readyState = 'ended';
                            if (kind === 'video' &&
                                typeof globalThis.__nd_camera_release === 'function')
                                globalThis.__nd_camera_release();
                            if (kind === 'audio' &&
                                typeof globalThis.__nd_mic_release === 'function')
                                globalThis.__nd_mic_release();
                            track.dispatchEvent({ type: 'ended' });
                        }
                    };
                    return track;
                }

                function makeStream(tracks) {
                    var L = {};
                    var stream = {
                        __proto__: globalThis.__ndMediaStream
                            ? globalThis.__ndMediaStream.prototype : null,
                        _tracks: tracks,
                        id: 'stream-' + rnd(),
                        active: true, _nd_camera: true,
                        onaddtrack: null, onremovetrack: null,
                        getTracks: function () { return tracks.slice(); },
                        getVideoTracks: function () {
                            return tracks.filter(function (t) { return t.kind === 'video'; });
                        },
                        getAudioTracks: function () {
                            return tracks.filter(function (t) { return t.kind === 'audio'; });
                        },
                        getTrackById: function (id) {
                            for (var i = 0; i < tracks.length; i++)
                                if (tracks[i].id === id) return tracks[i];
                            return null;
                        },
                        addTrack: function (t) { if (tracks.indexOf(t) < 0) tracks.push(t); },
                        removeTrack: function (t) {
                            var i = tracks.indexOf(t); if (i >= 0) tracks.splice(i, 1);
                        },
                        clone: function () {
                            return makeStream(tracks.map(function (t) { return t.clone(); }));
                        },
                        addEventListener: function (t, f) {
                            if (typeof f === 'function') (L[t] = L[t] || []).push(f);
                        },
                        removeEventListener: function (t, f) {
                            var a = L[t]; if (!a) return;
                            var i = a.indexOf(f); if (i >= 0) a.splice(i, 1);
                        },
                        dispatchEvent: function (e) {
                            var a = e && L[e.type];
                            if (a) a.slice().forEach(function (fn) {
                                try { fn.call(stream, e); } catch (_) {}
                            });
                            return true;
                        }
                    };
                    return stream;
                }

                function build(wantVideo, wantAudio) {
                    var tracks = [];
                    if (wantVideo)
                        tracks.push(makeTrack('video',
                            typeof globalThis.__nd_camera_label === 'function'
                                ? globalThis.__nd_camera_label() : 'Camera'));
                    if (wantAudio) tracks.push(makeTrack('audio', 'Microphone'));
                    return makeStream(tracks);
                }

                md.getUserMedia = nativeize(function getUserMedia(constraints) {
                    constraints = constraints || {};
                    var wantVideo = !!constraints.video;
                    var wantAudio = !!constraints.audio;
                    return new Promise(function (resolve, reject) {
                        if (!wantVideo && !wantAudio) {
                            reject(new TypeError('getUserMedia: no media requested'));
                            return;
                        }
                        var decision = typeof globalThis.__nd_camera_request === 'function'
                            ? globalThis.__nd_camera_request(wantVideo, wantAudio) : 'denied';
                        if (decision === 'granted')
                            resolve(build(wantVideo, wantAudio));
                        else if (decision === 'denied')
                            reject(makeError('NotAllowedError', 'Permission denied'));
                        else
                            pending.push({ wantVideo: wantVideo, wantAudio: wantAudio,
                                           resolve: resolve, reject: reject });
                    });
                });

                md.enumerateDevices = nativeize(function enumerateDevices() {
                    return new Promise(function (resolve) {
                        var list = typeof globalThis.__nd_camera_enumerate === 'function'
                            ? globalThis.__nd_camera_enumerate() : [];
                        resolve((list || []).map(function (d) {
                            return {
                                deviceId: d.deviceId, groupId: d.groupId,
                                kind: d.kind, label: d.label,
                                toJSON: function () { return this; }
                            };
                        }));
                    });
                });

                globalThis.__nd_camera_resolve_pending = function (allow) {
                    var q = pending; pending = [];
                    q.forEach(function (p) {
                        if (!allow) {
                            p.reject(makeError('NotAllowedError', 'Permission denied'));
                            return;
                        }
                        var d = typeof globalThis.__nd_camera_request === 'function'
                            ? globalThis.__nd_camera_request(p.wantVideo, p.wantAudio)
                            : 'denied';
                        if (d === 'denied')
                            p.reject(makeError('NotAllowedError', 'Permission denied'));
                        else
                            p.resolve(build(p.wantVideo, p.wantAudio));
                    });
                };
            })();
        }
        if (navigator.userAgentData &&
            typeof navigator.userAgentData.toJSON !== 'function') {
            try {
                Object.defineProperty(navigator.userAgentData, 'toJSON', {
                    configurable: true, enumerable: false,
                    value: function () {
                        return {
                            brands: this.brands || [],
                            mobile: !!this.mobile,
                            platform: String(this.platform || '')
                        };
                    }
                });
            } catch (e) {}
        }
    }

    var storageAccessTarget = typeof document !== 'undefined'
        ? document
        : global.Document && global.Document.prototype;
    if (storageAccessTarget) {
        try {
            Object.defineProperty(storageAccessTarget, 'hasStorageAccess', {
                configurable: true, enumerable: true,
                value: function () { return Promise.resolve(true); }
            });
        } catch (e) {}
        try {
            Object.defineProperty(storageAccessTarget, 'requestStorageAccess', {
                configurable: true, enumerable: true,
                value: function () { return Promise.resolve(); }
            });
        } catch (e) {}
        try {
            Object.defineProperty(storageAccessTarget, 'requestStorageAccessFor', {
                configurable: true, enumerable: true,
                value: function () { return Promise.resolve(); }
            });
        } catch (e) {}
    }

    if (typeof global.CookieStore !== 'function') {
        defineCtor('CookieStore', function CookieStore() {
            throw new TypeError('Illegal constructor');
        });
    }

    if (!global.cookieStore) {
        var cookiePairFor = function (name) {
            var key = String(name || '');
            var parts = String(document.cookie || '').split(/;\s*/);
            for (var i = 0; i < parts.length; i++) {
                var eq = parts[i].indexOf('=');
                var n = eq >= 0 ? parts[i].slice(0, eq) : parts[i];
                if (n === key) {
                    return {
                        name: n,
                        value: eq >= 0 ? parts[i].slice(eq + 1) : '',
                        domain: '',
                        path: '/',
                        expires: null,
                        secure: false,
                        sameSite: 'lax'
                    };
                }
            }
            return null;
        };
        try {
            Object.defineProperty(global, 'cookieStore', {
                configurable: true, enumerable: true,
                value: {
                    get: function (name) {
                        if (name && typeof name === 'object') name = name.name;
                        return Promise.resolve(cookiePairFor(name));
                    },
                    getAll: function (query) {
                        var parts = String(document.cookie || '').split(/;\s*/);
                        var out = [];
                        var wanted = query && typeof query === 'object' ? query.name : query;
                        for (var i = 0; i < parts.length; i++) {
                            if (!parts[i]) continue;
                            var eq = parts[i].indexOf('=');
                            var name = eq >= 0 ? parts[i].slice(0, eq) : parts[i];
                            if (wanted && name !== String(wanted)) continue;
                            var item = cookiePairFor(name);
                            if (item) out.push(item);
                        }
                        return Promise.resolve(out);
                    },
                    set: function (name, value) {
                        if (name && typeof name === 'object') {
                            value = name.value;
                            name = name.name;
                        }
                        document.cookie = String(name || '') + '=' + String(value == null ? '' : value);
                        return Promise.resolve();
                    },
                    delete: function (name) {
                        if (name && typeof name === 'object') name = name.name;
                        document.cookie = String(name || '') + '=; Max-Age=0';
                        return Promise.resolve();
                    },
                    addEventListener: function () {},
                    removeEventListener: function () {},
                    dispatchEvent: function () { return true; }
                }
            });
        } catch (e) {}
        try {
            var cookieStoreProto = global.CookieStore && global.CookieStore.prototype;
            if (cookieStoreProto) {
                ['get', 'getAll', 'set', 'delete', 'addEventListener',
                 'removeEventListener', 'dispatchEvent'].forEach(function (name) {
                    if (typeof cookieStoreProto[name] !== 'function') {
                        Object.defineProperty(cookieStoreProto, name, {
                            configurable: true, writable: true,
                            value: global.cookieStore[name]
                        });
                    }
                });
                Object.setPrototypeOf(global.cookieStore, cookieStoreProto);
                if (global.EventTarget && global.EventTarget.prototype)
                    Object.setPrototypeOf(cookieStoreProto,
                                          global.EventTarget.prototype);
                Object.defineProperty(cookieStoreProto, Symbol.toStringTag,
                                      { configurable: true, value: 'CookieStore' });
            }
        } catch (e) {}
    }

    if (typeof global.Notification === 'function') {
        try {
            Object.defineProperty(global.Notification, 'permission', {
                configurable: true, enumerable: true,
                value: 'default'
            });
            Object.defineProperty(global.Notification, 'requestPermission', {
                configurable: true, enumerable: true,
                value: nativeize(function requestPermission(callback) {
                    if (typeof callback === 'function') callback('default');
                    return Promise.resolve('default');
                })
            });
        } catch (e) {}
    }

    if (typeof global.MediaMetadata !== 'function') {
        try {
            defineCtor('MediaMetadata', function (init) {
                init = init || {};
                this.title = String(init.title || '');
                this.artist = String(init.artist || '');
                this.album = String(init.album || '');
                this.artwork = Array.isArray(init.artwork) ? init.artwork.slice() : [];
            });
        } catch (e) {}
    }

    if (navigator && !navigator.mediaSession) {
        try {
            Object.defineProperty(navigator, 'mediaSession', {
                configurable: true, enumerable: true,
                value: {
                    metadata: null,
                    playbackState: 'none',
                    setActionHandler: function (action, handler) {
                        this['_handler_' + String(action)] =
                            typeof handler === 'function' ? handler : null;
                    },
                    setPositionState: function (state) {
                        this._positionState = state || null;
                    }
                }
            });
        } catch (e) {}
    }

    var mediaProto = global.HTMLMediaElement && global.HTMLMediaElement.prototype;
    if (mediaProto) {
        try {
            Object.defineProperty(mediaProto, 'requestPictureInPicture', {
                configurable: true, enumerable: true,
                value: function () {
                    if (typeof document !== 'undefined') {
                        try {
                            Object.defineProperty(document, 'pictureInPictureElement', {
                                configurable: true,
                                value: this
                            });
                        } catch (e) {}
                    }
                    return Promise.resolve(this);
                }
            });
        } catch (e) {}
        try {
            Object.defineProperty(mediaProto, 'disablePictureInPicture', {
                configurable: true, enumerable: true, writable: true,
                value: false
            });
        } catch (e) {}
        try {
            Object.defineProperty(mediaProto, 'webkitSupportsFullscreen', {
                configurable: true, enumerable: true,
                value: false
            });
            Object.defineProperty(mediaProto, 'webkitDisplayingFullscreen', {
                configurable: true, enumerable: true,
                value: false
            });
            Object.defineProperty(mediaProto, 'webkitPresentationMode', {
                configurable: true, enumerable: true,
                value: 'inline'
            });
            Object.defineProperty(mediaProto, 'webkitSupportsPresentationMode', {
                configurable: true, enumerable: true,
                value: function () { return false; }
            });
            Object.defineProperty(mediaProto, 'webkitEnterFullscreen', {
                configurable: true, enumerable: true,
                value: function () {}
            });
            Object.defineProperty(mediaProto, 'webkitExitFullscreen', {
                configurable: true, enumerable: true,
                value: function () {}
            });
            Object.defineProperty(mediaProto, 'webkitSetPresentationMode', {
                configurable: true, enumerable: true,
                value: function () {}
            });
        } catch (e) {}
        if (!('remote' in mediaProto)) {
            try {
                Object.defineProperty(mediaProto, 'remote', {
                    configurable: true, enumerable: true,
                    get: function () {
                        if (!this.__nd_remotePlayback) {
                            Object.defineProperty(this, '__nd_remotePlayback', {
                                configurable: true,
                                value: {
                                    state: 'disconnected',
                                    onconnect: null,
                                    onconnecting: null,
                                    ondisconnect: null,
                                    prompt: function () { return Promise.resolve(); },
                                    watchAvailability: function (callback) {
                                        if (typeof callback === 'function') {
                                            try { callback(false); } catch (e) {}
                                        }
                                        return Promise.resolve(1);
                                    },
                                    cancelWatchAvailability: function () { return Promise.resolve(); },
                                    addEventListener: function () {},
                                    removeEventListener: function () {},
                                    dispatchEvent: function () { return true; }
                                }
                            });
                        }
                        return this.__nd_remotePlayback;
                    }
                });
            } catch (e) {}
        }
        try {
            Object.defineProperty(mediaProto, 'disableRemotePlayback', {
                configurable: true, enumerable: true, writable: true,
                value: false
            });
        } catch (e) {}
    }

    var actualMediaProto = global.Element && global.Element.prototype;
    if (actualMediaProto && actualMediaProto !== mediaProto) {
        try {
            Object.defineProperty(actualMediaProto, 'requestPictureInPicture', {
                configurable: true, enumerable: true,
                value: function () {
                    if (typeof document !== 'undefined') {
                        try {
                            Object.defineProperty(document, 'pictureInPictureElement', {
                                configurable: true,
                                value: this
                            });
                        } catch (e) {}
                    }
                    return Promise.resolve(this);
                }
            });
        } catch (e) {}
        try {
            Object.defineProperty(actualMediaProto, 'disablePictureInPicture', {
                configurable: true, enumerable: true, writable: true,
                value: false
            });
        } catch (e) {}
        try {
            Object.defineProperty(actualMediaProto, 'webkitSupportsFullscreen', {
                configurable: true, enumerable: true,
                value: false
            });
            Object.defineProperty(actualMediaProto, 'webkitDisplayingFullscreen', {
                configurable: true, enumerable: true,
                value: false
            });
            Object.defineProperty(actualMediaProto, 'webkitPresentationMode', {
                configurable: true, enumerable: true,
                value: 'inline'
            });
            Object.defineProperty(actualMediaProto, 'webkitSupportsPresentationMode', {
                configurable: true, enumerable: true,
                value: function () { return false; }
            });
            Object.defineProperty(actualMediaProto, 'webkitEnterFullscreen', {
                configurable: true, enumerable: true,
                value: function () {}
            });
            Object.defineProperty(actualMediaProto, 'webkitExitFullscreen', {
                configurable: true, enumerable: true,
                value: function () {}
            });
            Object.defineProperty(actualMediaProto, 'webkitSetPresentationMode', {
                configurable: true, enumerable: true,
                value: function () {}
            });
        } catch (e) {}
        if (!('remote' in actualMediaProto)) {
            try {
                Object.defineProperty(actualMediaProto, 'remote', {
                    configurable: true, enumerable: true,
                    get: function () {
                        if (!this.__nd_remotePlayback) {
                            Object.defineProperty(this, '__nd_remotePlayback', {
                                configurable: true,
                                value: {
                                    state: 'disconnected',
                                    onconnect: null,
                                    onconnecting: null,
                                    ondisconnect: null,
                                    prompt: function () { return Promise.resolve(); },
                                    watchAvailability: function (callback) {
                                        if (typeof callback === 'function') {
                                            try { callback(false); } catch (e) {}
                                        }
                                        return Promise.resolve(1);
                                    },
                                    cancelWatchAvailability: function () { return Promise.resolve(); },
                                    addEventListener: function () {},
                                    removeEventListener: function () {},
                                    dispatchEvent: function () { return true; }
                                }
                            });
                        }
                        return this.__nd_remotePlayback;
                    }
                });
            } catch (e) {}
        }
        try {
            Object.defineProperty(actualMediaProto, 'disableRemotePlayback', {
                configurable: true, enumerable: true, writable: true,
                value: false
            });
        } catch (e) {}
    }

    if (typeof document !== 'undefined') {
        try {
            Object.defineProperty(document, 'pictureInPictureEnabled', {
                configurable: true, enumerable: true,
                value: true
            });
            Object.defineProperty(document, 'exitPictureInPicture', {
                configurable: true, enumerable: true,
                value: function () {
                    try {
                        Object.defineProperty(document, 'pictureInPictureElement', {
                            configurable: true,
                            value: null
                        });
                    } catch (e) {}
                    return Promise.resolve();
                }
            });
        } catch (e) {}
    }

    if (typeof global.NavigationHistoryEntry !== 'function') {
        try {
            defineCtor('NavigationHistoryEntry', function () {
                this.id = '0';
                this.key = '0';
                this.index = 0;
                this.url = String(global.location && global.location.href || '');
                this.sameDocument = true;
            });
            global.NavigationHistoryEntry.prototype.getState = function () { return null; };
        } catch (e) {}
    }

    if (typeof global.Navigation !== 'function') {
        try {
            defineCtor('Navigation', function () {});
        } catch (e) {}
    }

    if (!global.navigation) {
        try {
            var makeNavEntry = function () {
                return {
                    id: '0',
                    key: '0',
                    index: 0,
                    url: String(global.location && global.location.href || ''),
                    sameDocument: true,
                    getState: function () { return null; }
                };
            };
            var makeNavResult = function () {
                var done = Promise.resolve(makeNavEntry());
                return { committed: done, finished: done };
            };
            Object.defineProperty(global, 'navigation', {
                configurable: true, enumerable: true,
                value: {
                    currentEntry: makeNavEntry(),
                    transition: null,
                    activation: null,
                    canGoBack: false,
                    canGoForward: false,
                    onnavigate: null,
                    onnavigatesuccess: null,
                    onnavigateerror: null,
                    oncurrententrychange: null,
                    entries: function () { return [this.currentEntry]; },
                    updateCurrentEntry: function (options) {
                        if (options && 'state' in options) this._state = options.state;
                    },
                    navigate: function (url) {
                        if (url != null) this.currentEntry.url = String(url);
                        return makeNavResult();
                    },
                    reload: function () { return makeNavResult(); },
                    traverseTo: function () { return makeNavResult(); },
                    back: function () { return makeNavResult(); },
                    forward: function () { return makeNavResult(); },
                    addEventListener: function () {},
                    removeEventListener: function () {},
                    dispatchEvent: function () { return true; }
                }
            });
        } catch (e) {}
    }

    if (typeof global.trustedTypes === 'undefined') {
        var trustedValue = new WeakMap();
        function makeTrustedCtor(name) {
            var ctor = function () { throw new TypeError('Illegal constructor'); };
            nativeize(ctor, name);
            var proto = {};
            Object.defineProperty(proto, 'toString', {
                configurable: true, writable: true,
                value: nativeize(function toString() {
                    if (!trustedValue.has(this)) throw new TypeError('Illegal invocation');
                    return trustedValue.get(this);
                }, 'toString')
            });
            Object.defineProperty(proto, 'toJSON', {
                configurable: true, writable: true,
                value: nativeize(function toJSON() {
                    if (!trustedValue.has(this)) throw new TypeError('Illegal invocation');
                    return trustedValue.get(this);
                }, 'toJSON')
            });
            Object.defineProperty(proto, Symbol.toStringTag,
                { value: name, configurable: true });
            Object.defineProperty(proto, 'constructor',
                { value: ctor, configurable: true, writable: true });
            Object.defineProperty(ctor, 'prototype', { value: proto });
            replaceCtor(name, ctor);
            return ctor;
        }
        var TrustedHTMLCtor = makeTrustedCtor('TrustedHTML');
        var TrustedScriptCtor = makeTrustedCtor('TrustedScript');
        var TrustedScriptURLCtor = makeTrustedCtor('TrustedScriptURL');
        function trusted(Ctor, value) {
            var result = Object.create(Ctor.prototype);
            trustedValue.set(result, String(value == null ? '' : value));
            return result;
        }
        var policyRules = new WeakMap();
        function TrustedTypePolicy() { throw new TypeError('Illegal constructor'); }
        nativeize(TrustedTypePolicy, 'TrustedTypePolicy');
        ['HTML', 'Script', 'ScriptURL'].forEach(function (kind) {
            var method = 'create' + kind;
            var Ctor = kind === 'HTML' ? TrustedHTMLCtor
                : kind === 'Script' ? TrustedScriptCtor : TrustedScriptURLCtor;
            Object.defineProperty(TrustedTypePolicy.prototype, method, {
                configurable: true, writable: true,
                value: nativeize(function (input) {
                    var record = policyRules.get(this);
                    if (!record) throw new TypeError('Illegal invocation');
                    var rule = record.rules[method];
                    if (typeof rule !== 'function') throw new TypeError('Missing policy rule');
                    var args = Array.prototype.slice.call(arguments, 1);
                    var value = rule.apply(undefined, [input].concat(args));
                    return trusted(Ctor, value);
                }, method)
            });
        });
        Object.defineProperty(TrustedTypePolicy.prototype, 'name', {
            configurable: true, enumerable: true,
            get: nativeize(function name() {
                var record = policyRules.get(this);
                if (!record) throw new TypeError('Illegal invocation');
                return record.name;
            }, 'get name')
        });
        Object.defineProperty(TrustedTypePolicy.prototype, Symbol.toStringTag,
            { value: 'TrustedTypePolicy', configurable: true });
        replaceCtor('TrustedTypePolicy', TrustedTypePolicy);

        var defaultPolicy = null;
        /* The factory's brand is a private name, which a frame's own copy
         * of the factory carries over (ns_realm_clone_instance). */
        var FactoryBrand = class extends (function (o) { return o; }) {
            #factory;
            static has(o) { return o !== null && typeof o === 'object' && #factory in o; }
        };
        function requireFactory(value) {
            if (!FactoryBrand.has(value)) throw new TypeError('Illegal invocation');
        }
        function TrustedTypePolicyFactory() { throw new TypeError('Illegal constructor'); }
        nativeize(TrustedTypePolicyFactory, 'TrustedTypePolicyFactory');
        var factoryProto = TrustedTypePolicyFactory.prototype;
        defineMethod(factoryProto, 'createPolicy', nativeize(function createPolicy(name, rules) {
            requireFactory(this);
            name = String(name);
            if (name === 'default' && defaultPolicy) throw new TypeError('Policy already exists');
            rules = rules == null ? {} : Object(rules);
            var savedRules = Object.create(null);
            ['createHTML', 'createScript', 'createScriptURL'].forEach(function (method) {
                var rule = rules[method];
                if (rule !== undefined && typeof rule !== 'function')
                    throw new TypeError(method + ' must be callable');
                if (rule !== undefined) savedRules[method] = rule;
            });
            var policy = Object.create(TrustedTypePolicy.prototype);
            policyRules.set(policy, { name: name, rules: savedRules });
            if (name === 'default') defaultPolicy = policy;
            return policy;
        }, 'createPolicy'));
        defineMethod(factoryProto, 'isHTML', nativeize(function isHTML(value) {
            requireFactory(this);
            return trustedValue.has(value) && value instanceof TrustedHTMLCtor;
        }, 'isHTML'));
        defineMethod(factoryProto, 'isScript', nativeize(function isScript(value) {
            requireFactory(this);
            return trustedValue.has(value) && value instanceof TrustedScriptCtor;
        }, 'isScript'));
        defineMethod(factoryProto, 'isScriptURL', nativeize(function isScriptURL(value) {
            requireFactory(this);
            return trustedValue.has(value) && value instanceof TrustedScriptURLCtor;
        }, 'isScriptURL'));
        defineMethod(factoryProto, 'getAttributeType', nativeize(function getAttributeType() {
            requireFactory(this);
            return null;
        }, 'getAttributeType'));
        defineMethod(factoryProto, 'getPropertyType', nativeize(function getPropertyType() {
            requireFactory(this);
            return null;
        }, 'getPropertyType'));
        Object.defineProperty(factoryProto, 'emptyHTML', {
            configurable: true, enumerable: true,
            get: nativeize(function emptyHTML() {
                requireFactory(this);
                return trusted(TrustedHTMLCtor, '');
            }, 'get emptyHTML')
        });
        Object.defineProperty(factoryProto, 'emptyScript', {
            configurable: true, enumerable: true,
            get: nativeize(function emptyScript() {
                requireFactory(this);
                return trusted(TrustedScriptCtor, '');
            }, 'get emptyScript')
        });
        Object.defineProperty(factoryProto, 'defaultPolicy', {
            configurable: true, enumerable: true,
            get: nativeize(function defaultPolicyGetter() {
                requireFactory(this);
                return defaultPolicy;
            }, 'get defaultPolicy')
        });
        Object.defineProperty(factoryProto, Symbol.toStringTag,
            { value: 'TrustedTypePolicyFactory', configurable: true });
        replaceCtor('TrustedTypePolicyFactory', TrustedTypePolicyFactory);
        var factory = Object.create(factoryProto);
        new FactoryBrand(factory);
        Object.defineProperty(global, 'trustedTypes', {
            value: factory, writable: false, configurable: true
        });
    }

    if (typeof global.scheduler === 'undefined') {
        defineCtor('scheduler', {
            postTask: function (callback, options) {
                var priority = options && options.priority;
                var delay = (options && options.delay) || 0;
                var signal = options && options.signal;
                return new Promise(function (resolve, reject) {
                    if (signal && signal.aborted) {
                        reject(signal.reason || new Error('AbortError'));
                        return;
                    }
                    var fire = function () {
                        if (signal && signal.aborted) {
                            reject(signal.reason || new Error('AbortError'));
                            return;
                        }
                        try { resolve(callback()); } catch (e) { reject(e); }
                    };
                    if (priority === 'background' || delay > 0) {
                        setTimeout(fire, delay);
                    } else {
                        Promise.resolve().then(fire);
                    }
                    if (signal && typeof signal.addEventListener === 'function') {
                        signal.addEventListener('abort', function () {
                            reject(signal.reason || new Error('AbortError'));
                        });
                    }
                });
            },
            yield: function () {
                if (typeof setTimeout === 'function') {
                    return new Promise(function (resolve) {
                        setTimeout(resolve, 0);
                    });
                }
                return Promise.resolve();
            }
        });
    }

    if (navigator) {
        try {
            if (!navigator.scheduling) {
                Object.defineProperty(navigator, 'scheduling', {
                    configurable: true, enumerable: true,
                    value: {
                        isInputPending: function () { return false; }
                    }
                });
            } else if (typeof navigator.scheduling.isInputPending !== 'function') {
                Object.defineProperty(navigator.scheduling, 'isInputPending', {
                    configurable: true, enumerable: true,
                    value: function () { return false; }
                });
            }
        } catch (e) {}
    }

    if (!global.visualViewport) {
        try {
            var visualViewport = {
                offsetLeft: 0,
                offsetTop: 0,
                scale: 1,
                onresize: null,
                onscroll: null,
                onscrollend: null,
                addEventListener: function () {},
                removeEventListener: function () {},
                dispatchEvent: function () { return true; }
            };
            Object.defineProperty(visualViewport, 'width', {
                configurable: true, enumerable: true,
                get: function () { return Number(global.innerWidth) || 0; }
            });
            Object.defineProperty(visualViewport, 'height', {
                configurable: true, enumerable: true,
                get: function () { return Number(global.innerHeight) || 0; }
            });
            Object.defineProperty(visualViewport, 'pageLeft', {
                configurable: true, enumerable: true,
                get: function () { return Number(global.scrollX || global.pageXOffset) || 0; }
            });
            Object.defineProperty(visualViewport, 'pageTop', {
                configurable: true, enumerable: true,
                get: function () { return Number(global.scrollY || global.pageYOffset) || 0; }
            });
            Object.defineProperty(global, 'visualViewport', {
                configurable: true, enumerable: true,
                value: visualViewport
            });
        } catch (e) {}
    }

    if (typeof global.TaskSignal !== 'function') {
        try {
            var TaskSignal = function (priority) {
                this.aborted = false;
                this.reason = undefined;
                this.onabort = null;
                this.onprioritychange = null;
                this.priority = priority || 'user-visible';
                this._cbs = [];
            };
            if (typeof global.AbortSignal === 'function' && global.AbortSignal.prototype) {
                TaskSignal.prototype = Object.create(global.AbortSignal.prototype);
                TaskSignal.prototype.constructor = TaskSignal;
            }
            TaskSignal.prototype.addEventListener = function (type, cb) {
                if (type === 'abort' && typeof cb === 'function') this._cbs.push(cb);
            };
            TaskSignal.prototype.removeEventListener = function (type, cb) {
                if (type !== 'abort') return;
                var i = this._cbs.indexOf(cb);
                if (i >= 0) this._cbs.splice(i, 1);
            };
            TaskSignal.prototype.dispatchEvent = function (ev) {
                if (ev && ev.type === 'abort') {
                    if (typeof this.onabort === 'function') {
                        try { this.onabort.call(this, ev); } catch (e) {}
                    }
                    var cbs = this._cbs.slice();
                    for (var i = 0; i < cbs.length; i++) {
                        try { cbs[i].call(this, ev); } catch (e) {}
                    }
                }
                return true;
            };
            TaskSignal.prototype.throwIfAborted = function () {
                if (!this.aborted) return;
                throw this.reason || new Error('AbortError');
            };
            defineCtor('TaskSignal', TaskSignal);
        } catch (e) {}
    }

    if (typeof global.TaskController !== 'function') {
        try {
            defineCtor('TaskController', function (options) {
                options = options || {};
                var Signal = typeof global.TaskSignal === 'function' ? global.TaskSignal : global.AbortSignal;
                this.signal = new Signal(options.priority || 'user-visible');
            });
            global.TaskController.prototype.abort = function (reason) {
                var signal = this.signal;
                if (!signal || signal.aborted) return;
                signal.aborted = true;
                signal.reason = reason === undefined ? new Error('AbortError') : reason;
                if (typeof signal.dispatchEvent === 'function')
                    signal.dispatchEvent({ type: 'abort', target: signal });
            };
            global.TaskController.prototype.setPriority = function (priority) {
                if (!this.signal) return;
                this.signal.priority = priority || 'user-visible';
                if (typeof this.signal.onprioritychange === 'function') {
                    try {
                        this.signal.onprioritychange.call(this.signal, {
                            type: 'prioritychange',
                            target: this.signal,
                            previousPriority: undefined
                        });
                    } catch (e) {}
                }
            };
        } catch (e) {}
    }

    if (typeof global.ReportingObserver !== 'function') {
        try {
            var ReportingObserver = function (callback, options) {
                this._callback = typeof callback === 'function' ? callback : null;
                this._options = options || {};
                this._records = [];
                this._observing = false;
            };
            ReportingObserver.prototype.observe = function () {
                this._observing = true;
            };
            ReportingObserver.prototype.disconnect = function () {
                this._observing = false;
                this._records = [];
            };
            ReportingObserver.prototype.takeRecords = function () {
                var records = this._records.slice();
                this._records.length = 0;
                return records;
            };
            ReportingObserver.supportedTypes = ['deprecation', 'intervention', 'crash'];
            defineCtor('ReportingObserver', ReportingObserver);
        } catch (e) {}
    }


    var doc = global.document;
    if (doc && doc.implementation) {
        var liveImpl = doc.implementation;
        var realCreate = liveImpl && liveImpl.createHTMLDocument;
        var realCreateBroken = true;
        try {
            var probe = realCreate && realCreate.call(liveImpl, '');
            if (probe && probe.body) realCreateBroken = false;
        } catch (e) { realCreateBroken = true; }

        if (realCreateBroken) {
            var stubElement = function (tag) {
                var children = [];
                var attrs = {};
                var innerHtml = '';
                var textContent = '';
                var el = {
                    nodeType: 1,
                    tagName: String(tag).toUpperCase(),
                    nodeName: String(tag).toUpperCase(),
                    nodeValue: null,
                    childNodes: children,
                    children: children,
                    parentNode: null,
                    parentElement: null,
                    ownerDocument: null,
                    style: {},
                    href: '',
                    src: '',
                    appendChild: function (n) {
                        if (n) { children.push(n); if (n && typeof n === 'object') { try { n.parentNode = el; n.parentElement = el; } catch (e) {} } }
                        return n;
                    },
                    removeChild: function (n) {
                        var i = children.indexOf(n);
                        if (i >= 0) children.splice(i, 1);
                        if (n && typeof n === 'object') { try { n.parentNode = null; n.parentElement = null; } catch (e) {} }
                        return n;
                    },
                    insertBefore: function (n, ref) {
                        var i = ref ? children.indexOf(ref) : -1;
                        if (i < 0) children.push(n); else children.splice(i, 0, n);
                        if (n && typeof n === 'object') { try { n.parentNode = el; n.parentElement = el; } catch (e) {} }
                        return n;
                    },
                    replaceChild: function (n, ref) {
                        var i = children.indexOf(ref);
                        if (i >= 0) { children.splice(i, 1, n); if (n && typeof n === 'object') { try { n.parentNode = el; n.parentElement = el; } catch (e) {} } }
                        return ref;
                    },
                    contains: function (n) { return children.indexOf(n) >= 0; },
                    cloneNode: function () { var c = stubElement(tag); c.innerHTML = innerHtml; return c; },
                    setAttribute: function (k, v) { attrs[String(k)] = String(v == null ? '' : v); },
                    getAttribute: function (k) { var v = attrs[String(k)]; return v === undefined ? null : v; },
                    hasAttribute: function (k) { return Object.prototype.hasOwnProperty.call(attrs, String(k)); },
                    removeAttribute: function (k) { delete attrs[String(k)]; },
                    hasAttributes: function () { return Object.keys(attrs).length > 0; },
                    addEventListener: function () {},
                    removeEventListener: function () {},
                    dispatchEvent: function () { return true; },
                    getElementsByTagName: function () { return []; },
                    getElementsByClassName: function () { return []; },
                    querySelector: function () { return null; },
                    querySelectorAll: function () { return []; },
                    closest: function () { return null; },
                    matches: function () { return false; },
                    classList: {
                        add: function () {}, remove: function () {},
                        toggle: function () { return false; },
                        contains: function () { return false; },
                        replace: function () {},
                    },
                    attributes: attrs,
                };
                Object.defineProperty(el, 'firstChild', { configurable: true, get: function () { return children[0] || null; } });
                Object.defineProperty(el, 'lastChild', { configurable: true, get: function () { return children[children.length - 1] || null; } });
                Object.defineProperty(el, 'firstElementChild', { configurable: true, get: function () {
                    for (var i = 0; i < children.length; i++) if (children[i] && children[i].nodeType === 1) return children[i];
                    return null;
                } });
                Object.defineProperty(el, 'lastElementChild', { configurable: true, get: function () {
                    for (var i = children.length - 1; i >= 0; i--) if (children[i] && children[i].nodeType === 1) return children[i];
                    return null;
                } });
                Object.defineProperty(el, 'nextSibling', { configurable: true, get: function () {
                    var p = el.parentNode;
                    if (!p || !p.childNodes) return null;
                    var i = p.childNodes.indexOf(el);
                    return (i >= 0 && i + 1 < p.childNodes.length) ? p.childNodes[i + 1] : null;
                } });
                Object.defineProperty(el, 'previousSibling', { configurable: true, get: function () {
                    var p = el.parentNode;
                    if (!p || !p.childNodes) return null;
                    var i = p.childNodes.indexOf(el);
                    return (i > 0) ? p.childNodes[i - 1] : null;
                } });
                Object.defineProperty(el, 'innerHTML', {
                    configurable: true, enumerable: true,
                    get: function () { return innerHtml; },
                    set: function (v) {
                        innerHtml = String(v == null ? '' : v);
                        children.length = 0;
                        var depth = 0;
                        var re = /<(\/?)([\w-]+)([^>]*)>/g, m;
                        while ((m = re.exec(innerHtml)) !== null) {
                            if (m[1] === '/') {
                                if (depth > 0) depth--;
                            } else {
                                var selfClose = m[3].slice(-1) === '/' || /^(?:area|base|br|col|embed|hr|img|input|link|meta|param|source|track|wbr)$/i.test(m[2]);
                                if (depth === 0) {
                                    var child = stubElement(m[2]);
                                    child.parentNode = el; child.parentElement = el;
                                    children.push(child);
                                }
                                if (!selfClose) depth++;
                            }
                        }
                    },
                });
                Object.defineProperty(el, 'textContent', {
                    configurable: true, enumerable: true,
                    get: function () { return textContent; },
                    set: function (v) { textContent = String(v == null ? '' : v); children.length = 0; },
                });
                Object.defineProperty(el, 'outerHTML', {
                    configurable: true, enumerable: true,
                    get: function () { return '<' + tag + '>' + innerHtml + '</' + tag + '>'; },
                    set: function () {},
                });
                return el;
            };

            var stubBody = function () { return stubElement('body'); };

            var stubDocument = function (title) {
                var docStub = {
                    nodeType: 9,
                    nodeName: '#document',
                    title: title == null ? '' : String(title),
                    contentType: 'text/html',
                    compatMode: 'CSS1Compat',
                    location: { href: '' },
                    body: stubBody(),
                    head: stubElement('head'),
                    documentElement: stubElement('html'),
                    createElement: function (tag) { return stubElement(tag); },
                    createTextNode: function (t) { return { nodeType: 3, nodeValue: String(t == null ? '' : t), data: String(t == null ? '' : t) }; },
                    createComment: function (t) { return { nodeType: 8, nodeValue: String(t == null ? '' : t), data: String(t == null ? '' : t) }; },
                    createDocumentFragment: function () {
                        var frag = stubElement('#document-fragment');
                        frag.nodeType = 11; frag.nodeName = '#document-fragment'; frag.tagName = undefined;
                        return frag;
                    },
                    getElementsByTagName: function () { return []; },
                    getElementsByClassName: function () { return []; },
                    getElementById: function () { return null; },
                    querySelector: function () { return null; },
                    querySelectorAll: function () { return []; },
                    addEventListener: function () {},
                    removeEventListener: function () {},
                    dispatchEvent: function () { return true; },
                };
                docStub.implementation = {
                    hasFeature: function () { return true; },
                    createHTMLDocument: stubDocument,
                    createDocument: function () { return stubDocument(''); },
                    createDocumentType: function () { return {}; },
                };
                return docStub;
            };

            try {
                Object.defineProperty(doc, 'implementation', {
                    configurable: true, enumerable: true,
                    get: function () {
                        return {
                            hasFeature: function () { return true; },
                            createHTMLDocument: stubDocument,
                            createDocument: function () { return stubDocument(''); },
                            createDocumentType: function () { return {}; },
                        };
                    },
                });
            } catch (e) {}
        }
    }

    (function () {
        var doc = global.document;
        if (!doc || typeof doc.createElement !== 'function') return;
        if (typeof global.CSSStyleSheet === 'function' &&
            global.CSSStyleSheet.prototype &&
            typeof global.CSSStyleSheet.prototype.replaceSync === 'function')
            return;

        var hostSeq = 0;
        function isIdentChar(c) {
            return c >= 'a' && c <= 'z' || c >= 'A' && c <= 'Z' ||
                   c >= '0' && c <= '9' || c === '_' || c === '-';
        }
        function rewriteHostTokens(css, id) {
            var marker = '[data-nd-host="' + id + '"]';
            var out = '';
            for (var i = 0; i < css.length;) {
                if (css.substr(i, 10).toLowerCase() === '::slotted(') {
                    var j = i + 10, depth = 1, inner = j;
                    for (; j < css.length && depth; j++) {
                        if (css[j] === '(') depth++;
                        else if (css[j] === ')') { depth--; if (!depth) break; }
                    }
                    out += marker + ' > ' + css.slice(inner, j);
                    i = css[j] === ')' ? j + 1 : j;
                    continue;
                }
                if (css.substr(i, 5).toLowerCase() === ':host') {
                    if (css.substr(i + 5, 9).toLowerCase() === '-context(') {
                        var j = i + 14, depth = 1;
                        for (; j < css.length && depth; j++) {
                            if (css[j] === '(') depth++;
                            else if (css[j] === ')') depth--;
                        }
                        out += marker;
                        i = j;
                        continue;
                    }
                    if (css[i + 5] === '(') {
                        var j = i + 6, depth = 1, inner = j;
                        for (; j < css.length && depth; j++) {
                            if (css[j] === '(') depth++;
                            else if (css[j] === ')') { depth--; if (!depth) break; }
                        }
                        out += marker + css.slice(inner, j);
                        i = css[j] === ')' ? j + 1 : j;
                        continue;
                    }
                    var nc = css[i + 5];
                    if (!nc || !isIdentChar(nc)) { out += marker; i += 5; continue; }
                }
                out += css[i];
                i++;
            }
            return out;
        }
        function scanSegment(css, i, end) {
            var quote = 0, paren = 0, bracket = 0;
            for (; i < end; i++) {
                var c = css[i];
                if (quote) { if (c === '\\' && i + 1 < end) i++; else if (c === quote) quote = 0; }
                else if (c === '"' || c === "'") quote = c;
                else if (c === '/' && css[i + 1] === '*') {
                    i += 2; while (i + 1 < end && !(css[i] === '*' && css[i + 1] === '/')) i++;
                } else if (c === '(') paren++;
                else if (c === ')') { if (paren) paren--; }
                else if (c === '[') bracket++;
                else if (c === ']') { if (bracket) bracket--; }
                else if (!paren && !bracket && (c === '{' || c === ';' || c === '}')) return i;
            }
            return end;
        }
        function skipBlock(css, i, end) {
            var depth = 0, quote = 0;
            for (; i < end; i++) {
                var c = css[i];
                if (quote) { if (c === '\\' && i + 1 < end) i++; else if (c === quote) quote = 0; }
                else if (c === '"' || c === "'") quote = c;
                else if (c === '/' && css[i + 1] === '*') {
                    i += 2; while (i + 1 < end && !(css[i] === '*' && css[i + 1] === '/')) i++;
                } else if (c === '{') depth++;
                else if (c === '}') { depth--; if (depth === 0) return i + 1; }
            }
            return end;
        }
        function splitTopComma(s) {
            var res = [], depth = 0, bracket = 0, quote = 0, start = 0;
            for (var i = 0; i < s.length; i++) {
                var c = s[i];
                if (quote) { if (c === '\\' && i + 1 < s.length) i++; else if (c === quote) quote = 0; }
                else if (c === '"' || c === "'") quote = c;
                else if (c === '(') depth++;
                else if (c === ')') { if (depth) depth--; }
                else if (c === '[') bracket++;
                else if (c === ']') { if (bracket) bracket--; }
                else if (c === ',' && !depth && !bracket) { res.push(s.slice(start, i)); start = i + 1; }
            }
            res.push(s.slice(start));
            return res;
        }
        function scopeSelector(sel, id, marker) {
            sel = sel.replace(/^\s+|\s+$/g, '');
            if (!sel) return '';
            if (sel.indexOf(':host') >= 0 || sel.indexOf('::slotted') >= 0)
                return rewriteHostTokens(sel, id);
            return marker + ' ' + sel;
        }
        function scopeRuleList(css, start, end, id, marker) {
            var out = '', i = start;
            while (i < end) {
                while (i < end && /\s/.test(css[i])) i++;
                if (i >= end) break;
                if (css[i] === '/' && css[i + 1] === '*') {
                    i += 2; while (i + 1 < end && !(css[i] === '*' && css[i + 1] === '/')) i++; i += 2; continue;
                }
                if (css[i] === '}') { i++; continue; }
                if (css[i] === '@') {
                    var seg = scanSegment(css, i, end), term = css[seg], prelude = css.slice(i, seg);
                    if (term === '{') {
                        var be = skipBlock(css, seg, end);
                        if (/^@(media|supports|container|layer|scope)\b/i.test(prelude))
                            out += prelude + '{' + scopeRuleList(css, seg + 1, be - 1, id, marker) + '}';
                        else out += css.slice(i, be);
                        i = be;
                    } else { out += prelude; if (term === ';') { out += ';'; i = seg + 1; } else i = seg; }
                    continue;
                }
                var seg2 = scanSegment(css, i, end);
                if (css[seg2] !== '{') { i = (seg2 < end) ? seg2 + 1 : end; continue; }
                var be2 = skipBlock(css, seg2, end);
                var parts = splitTopComma(css.slice(i, seg2)), scoped = [];
                for (var k = 0; k < parts.length; k++) {
                    var sc = scopeSelector(parts[k], id, marker);
                    if (sc) scoped.push(sc);
                }
                out += scoped.join(', ') + '{' + css.slice(seg2 + 1, be2 > seg2 ? be2 - 1 : seg2) + '}';
                i = be2;
            }
            return out;
        }
        function scopeCss(css, id) {
            if (!id) return css;
            return scopeRuleList(css, 0, css.length, id, '[data-nd-host="' + id + '"]');
        }
        function hostScopeId(host) {
            if (!host || typeof host.getAttribute !== 'function') return null;
            var existing = host.getAttribute('data-nd-host');
            if (existing) return existing;
            var id = 'a' + (++hostSeq);
            try { host.setAttribute('data-nd-host', id); } catch (e) { return null; }
            return id;
        }

        function applyText(sheet) {
            var nodes = sheet.__nodes;
            for (var i = 0; i < nodes.length; i++) {
                try {
                    nodes[i].textContent =
                        scopeCss(sheet.__cssText || '', nodes[i].__ndScopeId);
                } catch (e) {}
            }
        }

        function CSSStyleSheet(options) {
            this.__cssText = '';
            this.__nodes = [];
            this.__mediaText = (options && options.media) || '';
            this.title = null;
            this.ownerNode = null;
            this.ownerRule = null;
            this.parentStyleSheet = null;
            this.type = 'text/css';
            this.disabled = !!(options && options.disabled);
        }
        CSSStyleSheet.prototype.replaceSync = function (text) {
            this.__cssText = String(text == null ? '' : text);
            applyText(this);
        };
        CSSStyleSheet.prototype.replace = function (text) {
            try { this.replaceSync(text); return Promise.resolve(this); }
            catch (e) { return Promise.reject(e); }
        };
        CSSStyleSheet.prototype.insertRule = function (rule, index) {
            this.__cssText += (this.__cssText ? '\n' : '') + String(rule);
            applyText(this);
            return typeof index === 'number' ? index : 0;
        };
        CSSStyleSheet.prototype.deleteRule = function () {};
        Object.defineProperty(CSSStyleSheet.prototype, 'cssRules', {
            configurable: true, get: function () { return []; }
        });
        Object.defineProperty(CSSStyleSheet.prototype, 'rules', {
            configurable: true, get: function () { return []; }
        });
        Object.defineProperty(global, 'CSSStyleSheet', {
            value: CSSStyleSheet, writable: true,
            configurable: true, enumerable: false
        });

        function materialize(target, sheets) {
            var scopeId = (target === doc) ? null : hostScopeId(target.host);
            var container = doc.head || doc.documentElement || doc.body;
            var live = [];
            if (!container || typeof container.appendChild !== 'function')
                return live;
            for (var i = 0; i < sheets.length; i++) {
                var s = sheets[i];
                if (!s || !(s instanceof CSSStyleSheet) || s.disabled) continue;
                var el = doc.createElement('style');
                el.setAttribute('data-adopted', '');
                el.__ndScopeId = scopeId;
                el.textContent = scopeCss(s.__cssText || '', scopeId);
                container.appendChild(el);
                s.__nodes.push(el);
                live.push({ sheet: s, node: el });
            }
            return live;
        }

        function defineAdopted(target) {
            if (!target) return;
            var store = [];
            var live = [];
            try {
                Object.defineProperty(target, 'adoptedStyleSheets', {
                    configurable: true, enumerable: true,
                    get: function () { return store; },
                    set: function (v) {
                        var arr = v ? Array.prototype.slice.call(v) : [];
                        for (var i = 0; i < live.length; i++) {
                            var ent = live[i];
                            var idx = ent.sheet.__nodes.indexOf(ent.node);
                            if (idx >= 0) ent.sheet.__nodes.splice(idx, 1);
                            if (ent.node.parentNode)
                                ent.node.parentNode.removeChild(ent.node);
                        }
                        live = materialize(target, arr);
                        store = arr;
                    }
                });
            } catch (e) {}
        }

        defineAdopted(doc);

        if (typeof global.Element === 'function' &&
            global.Element.prototype &&
            typeof global.Element.prototype.attachShadow === 'function') {
            var origAttach = global.Element.prototype.attachShadow;
            global.Element.prototype.attachShadow = function () {
                var root = origAttach.apply(this, arguments);
                if (root && !('adoptedStyleSheets' in root)) defineAdopted(root);
                return root;
            };
        }
    })();

    /* CSSOM rule model: document.styleSheets, HTMLStyleElement/LinkElement
     * .sheet, and a real CSSRule / CSSStyleRule / CSSGroupingRule tree backed
     * by the owner <style> node. The native bindings exposed an empty
     * styleSheets list and a null .sheet; the rules are parsed from the node's
     * text once, then insertRule/deleteRule/replace mutate the tree in place
     * and rebuild the node's text content, which the engine re-cascades.
     * Each CSSStyleRule's .style is a live native CSSStyleDeclaration. */
    (function () {
        if (typeof document === 'undefined') return;

        if (typeof global.CSSStyleDeclaration === 'function' &&
            global.CSSStyleDeclaration.prototype &&
            !(Symbol.iterator in global.CSSStyleDeclaration.prototype)) {
            try {
                Object.defineProperty(global.CSSStyleDeclaration.prototype,
                                      Symbol.iterator, {
                    configurable: true, writable: true,
                    value: function () {
                        var self = this, i = 0;
                        return {
                            next: function () {
                                if (i < (self.length >>> 0))
                                    return { value: self[i++], done: false };
                                return { value: undefined, done: true };
                            },
                            'return': function () { return { done: true }; }
                        };
                    }
                });
            } catch (e) {}
        }

        function ctorFor(name, parentProto) {
            var ctor = global[name];
            if (typeof ctor !== 'function') {
                ctor = function () {
                    throw new TypeError('Illegal constructor');
                };
                try {
                    Object.defineProperty(ctor, 'name',
                        { value: name, configurable: true });
                } catch (e) {}
                try {
                    Object.defineProperty(global, name, {
                        value: ctor, writable: true,
                        configurable: true, enumerable: false
                    });
                } catch (e) {}
            }
            if (parentProto && ctor.prototype &&
                Object.getPrototypeOf(ctor.prototype) !== parentProto) {
                try { Object.setPrototypeOf(ctor.prototype, parentProto); }
                catch (e) {}
            }
            if (parentProto && parentProto.constructor &&
                parentProto.constructor !== Object &&
                Object.getPrototypeOf(ctor) !== parentProto.constructor) {
                try { Object.setPrototypeOf(ctor, parentProto.constructor); }
                catch (e) {}
            }
            try {
                Object.defineProperty(ctor.prototype, Symbol.toStringTag, {
                    value: name, configurable: true
                });
            } catch (e) {}
            return ctor;
        }

        function getter(proto, name, fn) {
            try {
                Object.defineProperty(proto, name, {
                    configurable: true, enumerable: true, get: fn
                });
            } catch (e) {}
        }
        function accessor(proto, name, get, set) {
            try {
                Object.defineProperty(proto, name, {
                    configurable: true, enumerable: true, get: get, set: set
                });
            } catch (e) {}
        }
        function method(proto, name, fn) {
            try {
                Object.defineProperty(proto, name, {
                    configurable: true, writable: true,
                    enumerable: true, value: fn
                });
            } catch (e) {}
        }

        var CSSRule = ctorFor('CSSRule', Object.prototype);
        var CSSGroupingRule = ctorFor('CSSGroupingRule', CSSRule.prototype);
        var CSSStyleRule = ctorFor('CSSStyleRule', CSSGroupingRule.prototype);
        var CSSConditionRule = ctorFor('CSSConditionRule', CSSGroupingRule.prototype);
        var CSSMediaRule = ctorFor('CSSMediaRule', CSSConditionRule.prototype);
        var CSSSupportsRule = ctorFor('CSSSupportsRule', CSSConditionRule.prototype);
        var CSSFontFaceRule = ctorFor('CSSFontFaceRule', CSSRule.prototype);
        var CSSPageRule = ctorFor('CSSPageRule', CSSRule.prototype);
        var CSSImportRule = ctorFor('CSSImportRule', CSSRule.prototype);
        var CSSNamespaceRule = ctorFor('CSSNamespaceRule', CSSRule.prototype);
        var CSSKeyframesRule = ctorFor('CSSKeyframesRule', CSSRule.prototype);
        var CSSCounterStyleRule = ctorFor('CSSCounterStyleRule', CSSRule.prototype);
        var CSSPropertyRule = ctorFor('CSSPropertyRule', CSSRule.prototype);
        var CSSLayerBlockRule = ctorFor('CSSLayerBlockRule', CSSGroupingRule.prototype);
        var CSSContainerRule = ctorFor('CSSContainerRule', CSSConditionRule.prototype);
        var CSSScopeRule = ctorFor('CSSScopeRule', CSSGroupingRule.prototype);

        var CONSTANTS = {
            STYLE_RULE: 1, CHARSET_RULE: 2, IMPORT_RULE: 3, MEDIA_RULE: 4,
            FONT_FACE_RULE: 5, PAGE_RULE: 6, KEYFRAMES_RULE: 7,
            KEYFRAME_RULE: 8, MARGIN_RULE: 9, NAMESPACE_RULE: 10,
            COUNTER_STYLE_RULE: 11, SUPPORTS_RULE: 12,
            FONT_FEATURE_VALUES_RULE: 14
        };
        Object.keys(CONSTANTS).forEach(function (k) {
            var v = CONSTANTS[k];
            try {
                Object.defineProperty(CSSRule.prototype, k, {
                    value: v, enumerable: true, configurable: true
                });
                Object.defineProperty(CSSRule, k, {
                    value: v, enumerable: true, configurable: true
                });
            } catch (e) {}
        });

        getter(CSSRule.prototype, 'type', function () {
            return this.__type | 0;
        });
        getter(CSSRule.prototype, 'parentRule', function () {
            return this.__parentRule || null;
        });
        getter(CSSRule.prototype, 'parentStyleSheet', function () {
            return this.__parentStyleSheet || null;
        });
        accessor(CSSRule.prototype, 'cssText',
            function () {
                return typeof this.__cssText === 'function'
                    ? this.__cssText() : '';
            },
            function () {});

        getter(CSSPropertyRule.prototype, 'name', function () {
            return this.__name || '';
        });
        getter(CSSCounterStyleRule.prototype, 'name', function () {
            return this.__name || '';
        });
        getter(CSSPropertyRule.prototype, 'syntax', function () {
            var d = this.__descriptors || {};
            var s = unquoteDescriptor(d['syntax']);
            return s === null || s === undefined ? '' : s;
        });
        getter(CSSPropertyRule.prototype, 'inherits', function () {
            var d = this.__descriptors || {};
            return d['inherits'] === 'true';
        });
        getter(CSSPropertyRule.prototype, 'initialValue', function () {
            var d = this.__descriptors || {};
            return typeof d['initial-value'] === 'string'
                ? d['initial-value'] : null;
        });

        function notify(owner) {
            var s = owner;
            while (s && typeof s.__notify !== 'function') s = s.__parentStyleSheet;
            if (s && typeof s.__notify === 'function') s.__notify();
        }

        var styleDeclarationProto = global.CSSStyleDeclaration &&
                                    global.CSSStyleDeclaration.prototype;
        if (styleDeclarationProto && !styleDeclarationProto.__ndRuleMutation) {
            ['setProperty', 'removeProperty'].forEach(function (name) {
                var nativeMethod = styleDeclarationProto[name];
                if (typeof nativeMethod !== 'function') return;
                Object.defineProperty(styleDeclarationProto, name, {
                    configurable: true, writable: true,
                    value: function () {
                        var result = nativeMethod.apply(this, arguments);
                        if (this.__ndOwnerRule) notify(this.__ndOwnerRule);
                        return result;
                    }
                });
            });
            var cssTextDescriptor = Object.getOwnPropertyDescriptor(
                styleDeclarationProto, 'cssText');
            if (cssTextDescriptor && cssTextDescriptor.set) {
                Object.defineProperty(styleDeclarationProto, 'cssText', {
                    configurable: cssTextDescriptor.configurable,
                    enumerable: cssTextDescriptor.enumerable,
                    get: cssTextDescriptor.get,
                    set: function (value) {
                        cssTextDescriptor.set.call(this, value);
                        if (this.__ndOwnerRule) notify(this.__ndOwnerRule);
                    }
                });
            }
            Object.defineProperty(styleDeclarationProto, '__ndRuleMutation', {
                value: true, configurable: true
            });
        }

        function canonAnB(raw) {
            var s = String(raw);
            if (/^\s*even\s*$/i.test(s)) return '2n';
            if (/^\s*odd\s*$/i.test(s)) return '2n+1';
            var mi = /^\s*([+-]?\d+)\s*$/.exec(s);
            if (mi) return String(parseInt(mi[1], 10));
            var m = /^\s*([+-]?\d*)n\s*(?:([+-])\s*(\d+))?\s*$/i.exec(s);
            if (!m) return null;
            var aStr = m[1];
            var A = (aStr === '' || aStr === '+') ? 1
                  : aStr === '-' ? -1 : parseInt(aStr, 10);
            var B = m[2] ? parseInt(m[2] + m[3], 10) : 0;
            var out = A === 1 ? 'n' : A === -1 ? '-n' : A + 'n';
            if (B > 0) out += '+' + B;
            else if (B < 0) out += '-' + (-B);
            return out;
        }
        var ANB_RE = /:(nth-child|nth-last-child|nth-of-type|nth-last-of-type)\(([^)]*)\)/gi;
        function selectorSpaces(sel) {
            var out = '', quote = '', pending = false;
            for (var i = 0; i < sel.length; i++) {
                var c = sel.charAt(i);
                if (quote) {
                    out += c;
                    if (c === '\\') {
                        if (i + 1 < sel.length) out += sel.charAt(++i);
                    } else if (c === quote) quote = '';
                    continue;
                }
                if (c === '/' && sel.charAt(i + 1) === '*') {
                    var close = sel.indexOf('*/', i + 2);
                    i = close < 0 ? sel.length : close + 1;
                } else if (c === '\\' && i + 1 < sel.length) {
                    if (pending && out) out += ' ';
                    pending = false;
                    out += c + sel.charAt(++i);
                } else if (c === '"' || c === "'") {
                    if (pending && out) out += ' ';
                    pending = false;
                    quote = c;
                    out += c;
                } else if (/\s/.test(c)) {
                    pending = true;
                } else {
                    if (pending && out) out += ' ';
                    pending = false;
                    out += c;
                }
            }
            return out;
        }
        function anbPart(arg) {
            var ofIdx = arg.toLowerCase().indexOf(' of ');
            return ofIdx >= 0 ? arg.slice(0, ofIdx) : arg;
        }
        function canonSelector(sel, namespaces) {
            if (!sel) return sel;
            sel = selectorSpaces(sel.replace(/^\s+|\s+$/g, ''))
                     .replace(/(^|[\s>+~,])(\*|[-_a-zA-Z][-_a-zA-Z0-9]*)\|/g,
                              function (m, lead, prefix) {
                         if (prefix === '*')
                             return namespaces && namespaces.defaultURI
                                 ? m : lead;
                         return namespaces && namespaces.defaultURI &&
                                namespaces.prefixes[prefix] === namespaces.defaultURI
                             ? lead : m;
                     })
                     .replace(/\[\|(?=[-*a-zA-Z_])/g, '[')
                     .replace(/(["'])\s*([is])\s*\]/gi, function (m, q, flag) {
                         return q + ' ' + flag.toLowerCase() + ']';
                     })
                     .replace(/(^|[\s>+~,])\*(?=[.#[:])/g, '$1')
                     .replace(/(^|[^:]):(before|after|first-line|first-letter)\b/gi,
                              '$1::$2')
                     .replace(/:(lang|not)\(\s*([^()]*)\s*\)/gi,
                              function (m, name, arg) {
                         return ':' + name.toLowerCase() + '(' +
                                arg.replace(/^\s+|\s+$/g, '') + ')';
                     })
                     .replace(/\\([0-9a-f]{1,6})(?=[^\s0-9a-f])/gi, '\\$1 ');
            return sel.replace(ANB_RE, function (m, fn, arg) {
                var ofIdx = arg.toLowerCase().indexOf(' of ');
                var rest = ofIdx >= 0 ? arg.slice(ofIdx) : '';
                var c = canonAnB(anbPart(arg));
                return c === null ? m
                    : ':' + fn.toLowerCase() + '(' + c + rest + ')';
            });
        }
        function selectorAnBValid(sel) {
            ANB_RE.lastIndex = 0;
            var m;
            while ((m = ANB_RE.exec(sel)))
                if (canonAnB(anbPart(m[2])) === null) return false;
            return true;
        }
        function selectorLooksNested(sel) {
            if (/&(?:[a-zA-Z_]|\\|-[a-zA-Z_])/.test(sel.replace(/"[^"]*"|'[^']*'/g, '')))
                return false;
            if (sel.indexOf('&') !== -1 || /^\s*[>+~]/.test(sel)) return true;
            var bare = sel.replace(/"[^"]*"|'[^']*'/g, '');
            return /[|]/.test(bare.replace(/\|\||\|=/g, ''));
        }
        function selectorParses(sel) {
            try { document.querySelectorAll(sel); return true; }
            catch (e) { return false; }
        }
        function preludeSelectorValid(sel) {
            if (!selectorAnBValid(sel)) return false;
            if (selectorLooksNested(sel)) return true;
            return selectorParses(sel);
        }
        function closingParen(text, open) {
            var depth = 0, quote = 0;
            for (var i = open; i < text.length; i++) {
                var c = text.charAt(i);
                if (quote) {
                    if (c === '\\') i++;
                    else if (c === quote) quote = 0;
                } else if (c === '"' || c === "'") quote = c;
                else if (c === '(') depth++;
                else if (c === ')' && --depth === 0) return i;
            }
            return -1;
        }
        function scopeSelectorValid(sel) {
            if (sel.indexOf('::') !== -1) return false;
            var relative = sel.replace(/^\s*[>+~]/, '')
                              .replace(/&/g, ':scope')
                              .replace(/^\s+|\s+$/g, '');
            if (!relative) return false;
            return selectorAnBValid(sel) && selectorParses(relative);
        }
        function scopeSelectorList(text) {
            var parts = splitTopLevel(text, ',').map(function (p) {
                return p.replace(/^\s+|\s+$/g, '');
            });
            if (!parts.length) return null;
            for (var i = 0; i < parts.length; i++)
                if (!scopeSelectorValid(parts[i])) return null;
            return parts.map(function (p) { return canonSelector(p); })
                        .join(', ');
        }
        function scopePrelude(prelude) {
            var rest = prelude.replace(/^@scope/i, '');
            if (rest !== '' && !/^[\s(]/.test(rest)) return null;
            rest = rest.replace(/^\s+|\s+$/g, '');
            var out = '@scope';
            if (rest.charAt(0) === '(') {
                var end = closingParen(rest, 0);
                if (end < 0) return null;
                var root = scopeSelectorList(rest.slice(1, end));
                if (root === null) return null;
                out += ' (' + root + ')';
                rest = rest.slice(end + 1).replace(/^\s+|\s+$/g, '');
            }
            if (rest === '') return out;
            if (!/^to\s*\(/i.test(rest)) return null;
            var open = rest.indexOf('(');
            var close = closingParen(rest, open);
            if (close < 0) return null;
            var limit = scopeSelectorList(rest.slice(open + 1, close));
            if (limit === null) return null;
            if (rest.slice(close + 1).replace(/^\s+|\s+$/g, '') !== '')
                return null;
            return out + ' to (' + limit + ')';
        }
        accessor(CSSStyleRule.prototype, 'selectorText',
            function () {
                var text = canonSelector(this.__selector || '', this.__namespaces);
                return nestedInStyleRule(this) ? nestedSelectorText(text) : text;
            },
            function (v) {
                v = String(v);
                if (/^\s*-\s*$/.test(v)) return;
                if (nestedInStyleRule(this)) {
                    if (!preludeSelectorValid(v)) return;
                } else {
                    try { document.querySelectorAll(v); }
                    catch (e) { return; }
                }
                this.__selector = v.replace(/^\s+|\s+$/g, '');
                notify(this);
            });
        accessor(CSSStyleRule.prototype, 'style',
            function () { return this.__styleProxy || this.__style; },
            function (v) {
                try { this.__style.cssText = (v == null) ? '' : String(v); }
                catch (e) {}
                notify(this);
            });

        [CSSFontFaceRule.prototype, CSSPageRule.prototype].forEach(function (p) {
            accessor(p, 'style',
                function () { return this.__styleProxy || this.__style; },
                function (v) {
                    try { this.__style.cssText = (v == null) ? '' : String(v); }
                    catch (e) {}
                    notify(this);
                });
        });

        function declText(rule) {
            var style = rule.__style, out = [];
            try {
                for (var i = 0; i < (style.length >>> 0); i++) {
                    var name = style.item(i);
                    if (!name) continue;
                    var val = style.getPropertyValue(name);
                    var pri = style.getPropertyPriority(name);
                    out.push(name + ': ' + val + (pri ? ' !' + pri : '') + ';');
                }
            } catch (e) { return ''; }
            return out.join(' ');
        }
        method(CSSStyleRule.prototype, '__cssText', function () {
            var d = declText(this);
            var sel = this.selectorText;
            if (this.__rules && this.__rules.length) {
                var lines = [];
                if (d) lines.push('  ' + d);
                this.__rules.forEach(function (r) { lines.push('  ' + r.cssText); });
                return sel + ' {\n' + lines.join('\n') + '\n}';
            }
            return sel + (d ? ' { ' + d + ' }' : ' { }');
        });
        method(CSSFontFaceRule.prototype, '__cssText', function () {
            var d = declText(this);
            return '@font-face' + (d ? ' { ' + d + ' }' : ' { }');
        });
        accessor(CSSPageRule.prototype, 'selectorText',
            function () { return this.__selector || ''; },
            function (value) {
                var selector = String(value).replace(/^\s+|\s+$/g, '');
                if (selector &&
                    !/^(?:[-_a-zA-Z][-_a-zA-Z0-9]*)?(?::(?:left|right|first|blank))*$/i.test(selector))
                    return;
                this.__selector = selector.replace(/:(left|right|first|blank)/gi,
                                                    function (m) {
                                                        return m.toLowerCase();
                                                    });
                notify(this);
            });
        method(CSSPageRule.prototype, '__cssText', function () {
            var d = declText(this), selector = this.__selector || '';
            return '@page' + (selector ? ' ' + selector : '') +
                   (d ? ' { ' + d + ' }' : ' { }');
        });

        getter(CSSGroupingRule.prototype, 'cssRules', function () {
            return this.__ruleList;
        });
        getter(CSSGroupingRule.prototype, 'rules', function () {
            return this.__ruleList;
        });
        method(CSSGroupingRule.prototype, 'insertRule', function (text, index) {
            return insertInto(this, this.__rules, this.__ruleList,
                              text, index, false);
        });
        method(CSSGroupingRule.prototype, 'deleteRule', function (index) {
            return deleteFrom(this, this.__rules, this.__ruleList, index);
        });
        function splitTopLevel(text, sep) {
            var parts = [], depth = 0, start = 0;
            for (var i = 0; i < text.length; i++) {
                var c = text.charAt(i);
                if (c === '(') depth++;
                else if (c === ')') { if (depth) depth--; }
                else if (c === sep && depth === 0) {
                    parts.push(text.slice(start, i));
                    start = i + 1;
                }
            }
            parts.push(text.slice(start));
            return parts;
        }
        function serializeMediaFeature(f) {
            var inner = f.slice(1, -1).replace(/^\s+|\s+$/g, '');
            var ci = inner.indexOf(':');
            if (ci < 0) return '(' + inner.toLowerCase() + ')';
            var name = inner.slice(0, ci).replace(/^\s+|\s+$/g, '').toLowerCase();
            var val = inner.slice(ci + 1).replace(/^\s+|\s+$/g, '');
            return '(' + name + ': ' + val + ')';
        }
        function serializeMediaQuery(q) {
            q = q.replace(/^\s+|\s+$/g, '');
            if (!q) return '';
            var i = 0, n = q.length, features = [];
            while (i < n && q.charAt(i) !== '(') i++;
            var head = q.slice(0, i).replace(/\s+and\s*$/i, '')
                                    .replace(/^\s+|\s+$/g, '');
            while (i < n) {
                if (q.charAt(i) === '(') {
                    var d = 1, j = i + 1;
                    while (j < n && d > 0) {
                        var ch = q.charAt(j);
                        if (ch === '(') d++;
                        else if (ch === ')') d--;
                        j++;
                    }
                    features.push(serializeMediaFeature(q.slice(i, j)));
                    i = j;
                } else i++;
            }
            var modifier = '', type = '';
            if (head) {
                var toks = head.split(/\s+/);
                var first = toks[0].toLowerCase();
                if (first === 'not' || first === 'only') {
                    modifier = first;
                    type = (toks[1] || '').toLowerCase();
                } else {
                    type = first;
                }
            }
            if (modifier) {
                var s = type ? modifier + ' ' + type : modifier;
                if (features.length) s += ' and ' + features.join(' and ');
                return s;
            }
            if (type && type !== 'all') {
                var s2 = type;
                if (features.length) s2 += ' and ' + features.join(' and ');
                return s2;
            }
            if (type === 'all' && !features.length) return 'all';
            return features.join(' and ');
        }
        function serializeMediaList(text) {
            if (!text) return '';
            if (typeof global.__ndMediaListSerialize === 'function')
                return global.__ndMediaListSerialize(String(text));
            return splitTopLevel(text, ',').map(serializeMediaQuery)
                       .filter(function (q) { return q !== ''; })
                       .join(', ');
        }
        function makeMediaListObject(text, onChange) {
            var serialized = serializeMediaList(text);
            var items = serialized === '' ? [] :
                splitTopLevel(serialized, ',').map(function (q) {
                    return q.replace(/^\s+|\s+$/g, '');
                });
            var list = { length: 0 };
            function relist(notify) {
                for (var i = 0; i < list.length; i++) delete list[i];
                for (var j = 0; j < items.length; j++) list[j] = items[j];
                list.length = items.length;
                list.mediaText = items.join(', ');
                if (notify && typeof onChange === 'function')
                    onChange(list.mediaText);
            }
            relist(false);
            list.item = function (idx) {
                idx = idx >>> 0;
                return idx < items.length ? items[idx] : null;
            };
            list.appendMedium = function (medium) {
                var query = serializeMediaList(String(medium));
                if (!query || splitTopLevel(query, ',').length !== 1) return;
                if (items.indexOf(query) >= 0) return;
                items.push(query);
                relist(true);
            };
            list.deleteMedium = function (medium) {
                var query = serializeMediaList(String(medium));
                var at = items.indexOf(query);
                if (at < 0)
                    throw domError('NotFoundError',
                                   'medium "' + medium + '" not in list');
                items.splice(at, 1);
                relist(true);
            };
            list.toString = function () { return list.mediaText; };
            try {
                Object.defineProperty(list, Symbol.toStringTag, {
                    value: 'MediaList', configurable: true
                });
            } catch (e) {}
            return list;
        }

        method(CSSGroupingRule.prototype, '__header', function () {
            if (this.__at === 'media')
                return '@media ' + serializeMediaList(this.__condition);
            if (this.__at === 'container')
                return '@container ' + this.__condition;
            return this.__prelude;
        });
        method(CSSGroupingRule.prototype, '__cssText', function () {
            var inner = this.__rules.map(function (r) {
                return '  ' + r.cssText + '\n';
            }).join('');
            return this.__header() + ' {\n' + inner + '}';
        });

        accessor(CSSConditionRule.prototype, 'conditionText',
            function () {
                return this.__at === 'media'
                    ? serializeMediaList(this.__condition)
                    : (this.__condition || '');
            },
            function () {});
        accessor(CSSMediaRule.prototype, 'media',
            function () { return makeMediaListObject(this.__condition); },
            function () {});
        function containerSplit(cond) {
            var s = String(cond || '');
            if (/^\(|^not\s|^not\(/i.test(s)) return ['', s];
            var m = /^([^\s(]+)(?:\s+([\s\S]*))?$/.exec(s);
            if (m) return [m[1], m[2] || ''];
            return ['', s];
        }
        function containerSingle(rule, index) {
            var parts = containerParts(rule.__condition);
            return parts.length === 1 ? (index ? parts[0].query : parts[0].name) : '';
        }
        function containerParts(cond) {
            var s = String(cond || ''), out = [], depth = 0, start = 0;
            for (var i = 0; i < s.length; i++) {
                var c = s.charAt(i);
                if (c === '(') depth++;
                else if (c === ')') depth--;
                else if (c === ',' && depth === 0) {
                    out.push(s.slice(start, i).trim());
                    start = i + 1;
                }
            }
            out.push(s.slice(start).trim());
            return out.map(function (part) {
                var nq = containerSplit(part);
                return { name: nq[0], query: nq[1] };
            });
        }
        accessor(CSSContainerRule.prototype, 'conditions',
            function () { return containerParts(this.__condition); },
            function () {});
        accessor(CSSContainerRule.prototype, 'containerName',
            function () { return containerSingle(this, 0); },
            function () {});
        accessor(CSSContainerRule.prototype, 'containerQuery',
            function () { return containerSingle(this, 1); },
            function () {});

        function makeList() {
            var list = [];
            list.item = function (i) {
                i = i >>> 0;
                return (i < this.length) ? this[i] : null;
            };
            try {
                Object.defineProperty(list, Symbol.toStringTag, {
                    value: 'CSSRuleList', configurable: true
                });
            } catch (e) {}
            return list;
        }
        function syncList(list, rules) {
            for (var i = 0; i < rules.length; i++) list[i] = rules[i];
            list.length = rules.length;
        }

        function atKeyword(prelude) {
            var m = /^@([\w-]+)/.exec(prelude);
            return m ? m[1].toLowerCase() : null;
        }
        var GROUPING_AT = {
            media: 'CSSMediaRule', supports: 'CSSSupportsRule',
            container: 'CSSContainerRule', layer: 'CSSLayerBlockRule',
            scope: 'CSSScopeRule', document: 'CSSGroupingRule'
        };
        function splitDescriptors(block) {
            var s = String(block), out = [], buf = '', depth = 0, quote = 0;
            for (var i = 0; i < s.length; i++) {
                var c = s.charAt(i);
                if (quote) {
                    buf += c;
                    if (c === '\\') { buf += s.charAt(++i); continue; }
                    if (c === quote) quote = 0;
                    continue;
                }
                if (c === '"' || c === "'") { quote = c; buf += c; continue; }
                if (c === '(' || c === '[' || c === '{') depth++;
                else if (c === ')' || c === ']' || c === '}') depth--;
                else if (c === ';' && depth <= 0) { out.push(buf); buf = ''; continue; }
                buf += c;
            }
            out.push(buf);
            var map = Object.create(null);
            for (var k = 0; k < out.length; k++) {
                var decl = out[k];
                var colon = -1, d = 0, q = 0;
                for (var j = 0; j < decl.length; j++) {
                    var ch = decl.charAt(j);
                    if (q) { if (ch === '\\') j++; else if (ch === q) q = 0; continue; }
                    if (ch === '"' || ch === "'") { q = ch; continue; }
                    if (ch === '(' || ch === '[' || ch === '{') d++;
                    else if (ch === ')' || ch === ']' || ch === '}') d--;
                    else if (ch === ':' && d <= 0) { colon = j; break; }
                }
                if (colon < 0) continue;
                var name = decl.slice(0, colon).replace(/^\s+|\s+$/g, '')
                               .toLowerCase();
                if (name) map[name] = decl.slice(colon + 1)
                                          .replace(/^\s+|\s+$/g, '');
            }
            return map;
        }
        function unquoteDescriptor(value) {
            if (typeof value !== 'string') return null;
            var v = value.replace(/^\s+|\s+$/g, '');
            if (v.length < 2) return null;
            var q = v.charAt(0);
            if ((q !== '"' && q !== "'") || v.charAt(v.length - 1) !== q)
                return null;
            var body = v.slice(1, -1);
            if (body.indexOf(q) !== -1) return null;
            return body;
        }
        var CSS_WIDE = {
            initial: 1, inherit: 1, unset: 1, revert: 1, 'revert-layer': 1
        };
        var NON_OVERRIDABLE_COUNTER_STYLES = {
            decimal: 1, disc: 1, square: 1, circle: 1,
            'disclosure-open': 1, 'disclosure-closed': 1, none: 1
        };
        function identNameStart(ch) {
            return (ch >= 'a' && ch <= 'z') || (ch >= 'A' && ch <= 'Z') ||
                   ch === '_' || ch.charCodeAt(0) >= 0x80;
        }
        function identValid(name) {
            var i = 0;
            if (name.charAt(0) === '-') {
                if (name.charAt(1) === '-') return name.length >= 2;
                i = 1;
            }
            if (i >= name.length) return false;
            if (name.charAt(i) === '\\') { i += 2; }
            else if (identNameStart(name.charAt(i))) { i++; }
            else return false;
            for (; i < name.length; i++) {
                var ch = name.charAt(i);
                if (ch === '\\') { i++; continue; }
                if (!identNameStart(ch) && !(ch >= '0' && ch <= '9') &&
                    ch !== '-')
                    return false;
            }
            return true;
        }
        function counterStyleNameValid(name) {
            if (!name || !identValid(name)) return false;
            var lower = name.toLowerCase();
            return !CSS_WIDE[lower] && !NON_OVERRIDABLE_COUNTER_STYLES[lower];
        }

        function namespaceMap(parentRule) {
            if (parentRule && parentRule.__namespaces)
                return parentRule.__namespaces;
            return { defaultURI: '', prefixes: Object.create(null) };
        }
        function registerNamespace(prelude, namespaces) {
            var rest = prelude.replace(/^@namespace\s+/i, '')
                              .replace(/^\s+|\s+$/g, '');
            var prefix = '', split = rest.search(/\s/);
            if (split > 0 && !/^url\(/i.test(rest) &&
                rest.charAt(0) !== '"' && rest.charAt(0) !== "'") {
                prefix = rest.slice(0, split);
                rest = rest.slice(split).replace(/^\s+|\s+$/g, '');
            }
            var m = /^url\(\s*(['"]?)(.*?)\1\s*\)$/i.exec(rest);
            var uri = m ? m[2] : rest.replace(/^(['"])(.*)\1$/, '$2');
            if (prefix) namespaces.prefixes[prefix] = uri;
            else namespaces.defaultURI = uri;
        }
        function parseRuleList(text, sheet, parentRule, inheritedNamespaces) {
            var namespaces = inheritedNamespaces || namespaceMap(parentRule);
            if (!parentRule && sheet) sheet.__namespaces = namespaces;
            var rules = [], i = 0, n = text.length;
            while (i < n) {
                while (i < n && /\s/.test(text.charAt(i))) i++;
                if (i < n && text.charAt(i) === '/' && text.charAt(i + 1) === '*') {
                    i += 2;
                    while (i + 1 < n && !(text.charAt(i) === '*' &&
                                          text.charAt(i + 1) === '/')) i++;
                    i += 2; continue;
                }
                if (i >= n) break;
                if (text.charAt(i) === '}') { i++; continue; }
                var start = i, quote = 0, depth = 0;
                while (i < n) {
                    var c = text.charAt(i);
                    if (quote) {
                        if (c === '\\') i++;
                        else if (c === quote) quote = 0;
                        i++; continue;
                    }
                    if (c === '"' || c === "'") { quote = c; i++; continue; }
                    if (c === '/' && text.charAt(i + 1) === '*') {
                        i += 2;
                        while (i + 1 < n && !(text.charAt(i) === '*' &&
                                              text.charAt(i + 1) === '/')) i++;
                        i += 2; continue;
                    }
                    if (c === '(') { depth++; i++; continue; }
                    if (c === ')') { if (depth) depth--; i++; continue; }
                    if (!depth && (c === '{' || c === ';')) break;
                    i++;
                }
                var prelude = text.slice(start, i).replace(/^\s+|\s+$/g, '');
                if (i < n && text.charAt(i) === ';') {
                    i++;
                    if (prelude) {
                        var sr = makeAtStatement(prelude, sheet, parentRule);
                        if (sr) {
                            rules.push(sr);
                            if (sr.__at === 'namespace')
                                registerNamespace(prelude, namespaces);
                        }
                    }
                    continue;
                }
                if (i >= n || text.charAt(i) !== '{') {
                    break;
                }
                var bstart = ++i; depth = 1; quote = 0;
                while (i < n && depth > 0) {
                    var c2 = text.charAt(i);
                    if (quote) {
                        if (c2 === '\\') i++;
                        else if (c2 === quote) quote = 0;
                        i++; continue;
                    }
                    if (c2 === '"' || c2 === "'") { quote = c2; i++; continue; }
                    if (c2 === '/' && text.charAt(i + 1) === '*') {
                        i += 2;
                        while (i + 1 < n && !(text.charAt(i) === '*' &&
                                              text.charAt(i + 1) === '/')) i++;
                        i += 2; continue;
                    }
                    if (c2 === '{') depth++;
                    else if (c2 === '}') depth--;
                    i++;
                }
                var block = text.slice(bstart, (depth === 0) ? i - 1 : i);
                var br = makeBlockRule(prelude, block, sheet, parentRule,
                                       namespaces);
                if (br) rules.push(br);
            }
            return rules;
        }

        function serializeDeclBlock(block) {
            var s = String(block), out = [], buf = '', depth = 0, quote = 0;
            for (var i = 0; i < s.length; i++) {
                var c = s.charAt(i);
                if (quote) {
                    buf += c;
                    if (c === quote && s.charAt(i - 1) !== '\\') quote = 0;
                    continue;
                }
                if (c === '"' || c === "'") { quote = c; buf += c; continue; }
                if (c === '(') { depth++; buf += c; continue; }
                if (c === ')') { if (depth) depth--; buf += c; continue; }
                if (c === ';' && depth === 0) {
                    var d = buf.replace(/\s+/g, ' ').replace(/^ | $/g, '');
                    if (d) out.push(d + ';');
                    buf = '';
                    continue;
                }
                buf += c;
            }
            var last = buf.replace(/\s+/g, ' ').replace(/^ | $/g, '');
            if (last) out.push(last + ';');
            return out.join(' ');
        }

        function makeAtStatement(prelude, sheet, parentRule) {
            var kw = atKeyword(prelude);
            var StmtCtor = kw === 'import' ? CSSImportRule :
                           kw === 'namespace' ? CSSNamespaceRule : CSSRule;
            var rule = Object.create(StmtCtor.prototype);
            rule.__parentStyleSheet = sheet || null;
            rule.__parentRule = parentRule || null;
            rule.__at = kw;
            rule.__type = kw === 'import' ? 3 :
                          kw === 'namespace' ? 10 :
                          kw === 'charset' ? 2 : 0;
            var text = prelude + ';';
            rule.__cssText = function () { return text; };
            return rule;
        }

        function makeBlockRule(prelude, block, sheet, parentRule, namespaces) {
            var kw = atKeyword(prelude);
            if (kw === 'scope') {
                prelude = scopePrelude(prelude);
                if (prelude === null) return null;
            }
            if (kw && GROUPING_AT[kw]) {
                var Ctor = global[GROUPING_AT[kw]] || CSSGroupingRule;
                var g = Object.create(Ctor.prototype);
                g.__parentStyleSheet = sheet || null;
                g.__parentRule = parentRule || null;
                g.__at = kw;
                g.__prelude = prelude;
                g.__type = kw === 'media' ? 4 : kw === 'supports' ? 12 : 0;
                var cond = prelude.replace(/^@[\w-]+\s*/, '')
                                  .replace(/^\s+|\s+$/g, '');
                if (kw === 'container' &&
                    typeof global.__ns_container_query_canonical === 'function') {
                    cond = global.__ns_container_query_canonical(cond);
                    if (cond === null) return null;
                }
                g.__condition = cond;
                g.__namespaces = namespaces;
                g.__rules = parseRuleList(block, sheet, g, namespaces);
                g.__ruleList = makeList();
                syncList(g.__ruleList, g.__rules);
                return g;
            }
            if (kw === 'property' || kw === 'counter-style') {
                var atName = prelude.replace(/^@[\w-]+\s*/, '')
                                    .replace(/^\s+|\s+$/g, '');
                var descs = splitDescriptors(block);
                var ctor, ruleType;
                if (kw === 'property') {
                    if (atName.slice(0, 2) !== '--' || !identValid(atName))
                        return null;
                    var syntaxText = unquoteDescriptor(descs['syntax']);
                    var inheritsText = descs['inherits'];
                    var hasInherits = inheritsText === 'true' ||
                                      inheritsText === 'false';
                    var initialText =
                        typeof descs['initial-value'] === 'string'
                            ? descs['initial-value'] : null;
                    if (typeof global.__ns_property_rule_valid === 'function' &&
                        !global.__ns_property_rule_valid(
                            syntaxText === undefined ? null : syntaxText,
                            initialText, hasInherits))
                        return null;
                    ctor = CSSPropertyRule;
                    ruleType = 0;
                } else {
                    if (!counterStyleNameValid(atName)) return null;
                    ctor = CSSCounterStyleRule;
                    ruleType = 11;
                }
                var nr = Object.create(ctor.prototype);
                nr.__parentStyleSheet = sheet || null;
                nr.__parentRule = parentRule || null;
                nr.__at = kw;
                nr.__type = ruleType;
                nr.__name = atName;
                nr.__descriptors = descs;
                var nhead = prelude.replace(/\s+/g, ' ').replace(/^ | $/g, '');
                var nbody = serializeDeclBlock(block);
                var nraw = nhead + (nbody ? ' { ' + nbody + ' }' : ' { }');
                nr.__cssText = function () { return nraw; };
                return nr;
            }
            if (kw === 'keyframes' || kw === '-webkit-keyframes') {
                var kfName = prelude.replace(/^@[\w-]+\s*/, '').replace(/^\s+|\s+$/g, '');
                var quoted = /^"([^"\\]|\\.)*"$|^'([^'\\]|\\.)*'$/.test(kfName);
                var reserved = /^(none|default|initial|inherit|unset|revert|revert-layer)$/i;
                if (!kfName || (quoted && kfName.length === 2)) return null;
                if (!quoted && (!identValid(kfName) || reserved.test(kfName) ||
                                /^-?\d/.test(kfName)))
                    return null;
            }
            if (kw) {
                var atType = kw === 'font-face' ? 5 : kw === 'page' ? 6 :
                             kw === 'keyframes' || kw === '-webkit-keyframes' ? 7 : 0;
                var RuleCtor = atType === 5 ? CSSFontFaceRule :
                               atType === 6 ? CSSPageRule :
                               atType === 7 ? CSSKeyframesRule :
                               CSSRule;
                var ar = Object.create(RuleCtor.prototype);
                ar.__parentStyleSheet = sheet || null;
                ar.__parentRule = parentRule || null;
                ar.__at = kw;
                ar.__type = atType;
                if (kw === 'font-face' || kw === 'page') {
                    var holder = document.createElement('span');
                    try { holder.style.cssText = block; } catch (e) {}
                    ar.__holder = holder;
                    ar.__style = holder.style;
                    if (kw === 'page') {
                        ar.__selector = prelude.replace(/^@page\s*/i, '')
                                               .replace(/^\s+|\s+$/g, '');
                    }
                } else {
                    var head = prelude.replace(/\s+/g, ' ').replace(/^ | $/g, '');
                    var body = serializeDeclBlock(block);
                    var raw = head + (body ? ' { ' + body + ' }' : ' { }');
                    ar.__cssText = function () { return raw; };
                }
                return ar;
            }
            if (!preludeSelectorValid(prelude)) return null;
            var r = Object.create(CSSStyleRule.prototype);
            r.__parentStyleSheet = sheet || null;
            r.__parentRule = parentRule || null;
            r.__type = 1;
            r.__selector = prelude;
            r.__namespaces = namespaces;
            var split = splitStyleBlock(block);
            r.__rules = split.nested ? parseRuleList(split.nested, sheet, r, namespaces) : [];
            r.__ruleList = makeList();
            syncList(r.__ruleList, r.__rules);
            var holder = document.createElement('span');
            try { holder.style.cssText = split.decls; } catch (e) {}
            r.__holder = holder;
            r.__style = holder.style;
            try {
                Object.defineProperty(r.__style, '__ndOwnerRule', {
                    value: r, configurable: true
                });
                var liveCssText = Object.getOwnPropertyDescriptor(
                    styleDeclarationProto, 'cssText');
                if (liveCssText && liveCssText.set)
                    Object.defineProperty(r.__style, 'cssText', {
                        configurable: true,
                        get: function () {
                            return liveCssText.get.call(this);
                        },
                        set: function (value) {
                            liveCssText.set.call(this, value);
                        }
                    });
            } catch (e) {}
            if (typeof Proxy === 'function') {
                r.__styleProxy = new Proxy(r.__style, {
                    get: function (target, key) {
                        var value = Reflect.get(target, key, target);
                        return typeof value === 'function'
                            ? function () {
                                return value.apply(target, arguments);
                            } : value;
                    },
                    set: function (target, key, value) {
                        Reflect.set(target, key, value, target);
                        notify(r);
                        return true;
                    }
                });
            }
            return r;
        }

        function splitStyleBlock(block) {
            var s = String(block), decls = '', nested = '';
            var i = 0, n = s.length;
            while (i < n) {
                var start = i, quote = 0, depth = 0, sawBrace = false;
                while (i < n) {
                    var c = s.charAt(i);
                    if (quote) {
                        if (c === '\\') i++;
                        else if (c === quote) quote = 0;
                        i++; continue;
                    }
                    if (c === '"' || c === "'") { quote = c; i++; continue; }
                    if (c === '/' && s.charAt(i + 1) === '*') {
                        i += 2;
                        while (i + 1 < n && !(s.charAt(i) === '*' && s.charAt(i + 1) === '/')) i++;
                        i += 2; continue;
                    }
                    if (c === '(' || c === '[') { depth++; i++; continue; }
                    if (c === ')' || c === ']') { if (depth) depth--; i++; continue; }
                    if (!depth && c === '{') { sawBrace = true; break; }
                    if (!depth && c === ';') break;
                    i++;
                }
                if (!sawBrace) {
                    var d = s.slice(start, i).replace(/^\s+|\s+$/g, '');
                    if (d) decls += d + '; ';
                    i++;
                    continue;
                }
                var declHead = /^\s*(--[^\s:]+|[A-Za-z_-][\w-]*)\s*:([\s\S]*)$/.exec(s.slice(start, i));
                var braceStart = i;
                var bdepth = 0, q2 = 0;
                while (i < n) {
                    var c2 = s.charAt(i);
                    if (q2) {
                        if (c2 === '\\') i++;
                        else if (c2 === q2) q2 = 0;
                        i++; continue;
                    }
                    if (c2 === '"' || c2 === "'") { q2 = c2; i++; continue; }
                    if (c2 === '{') bdepth++;
                    else if (c2 === '}') { bdepth--; if (bdepth === 0) { i++; break; } }
                    i++;
                }
                if (declHead) {
                    var j = i, q3 = 0, pdepth = 0;
                    while (j < n) {
                        var c3 = s.charAt(j);
                        if (q3) {
                            if (c3 === '\\') j++;
                            else if (c3 === q3) q3 = 0;
                            j++; continue;
                        }
                        if (c3 === '"' || c3 === "'") { q3 = c3; j++; continue; }
                        if (c3 === '(' || c3 === '[') pdepth++;
                        else if (c3 === ')' || c3 === ']') { if (pdepth) pdepth--; }
                        else if (!pdepth && (c3 === ';' || c3 === '{')) break;
                        j++;
                    }
                    var tail = s.slice(i, j);
                    var isCustom = declHead[1].slice(0, 2) === '--';
                    var wholeBlock = declHead[2].replace(/\s+/g, '') === '' &&
                                     tail.replace(/\s+/g, '') === '';
                    if (s.charAt(j) !== '{' && (isCustom || wholeBlock)) {
                        var value = (declHead[2] + s.slice(braceStart, i) + tail).replace(/^\s+|\s+$/g, '');
                        decls += declHead[1] + ': ' + value + '; ';
                        i = j + 1;
                        continue;
                    }
                }
                nested += s.slice(start, i) + '\n';
            }
            return { decls: decls, nested: nested.replace(/^\s+|\s+$/g, '') };
        }

        function nestedInStyleRule(rule) {
            for (var p = rule.__parentRule; p; p = p.__parentRule)
                if (p.__type === 1) return true;
            return false;
        }

        function nestedSelectorText(sel) {
            var parts = splitTopLevel(sel, ',');
            return parts.map(function (part) {
                var t = part.replace(/^\s+|\s+$/g, '');
                if (!t) return t;
                var bare = t.replace(/"[^"]*"|'[^']*'/g, '');
                if (/^[>+~]/.test(t) || bare.indexOf('&') < 0) return '& ' + t;
                return t;
            }).join(', ');
        }

        function parseOne(text, sheet, parentRule) {
            var rules = parseRuleList(String(text), sheet, parentRule);
            if (rules.length !== 1) return null;
            return rules[0];
        }

        function domError(name, msg) {
            try { return new DOMException(msg || name, name); }
            catch (e) {
                var err = new Error(msg || name);
                err.name = name;
                return err;
            }
        }

        function insertInto(owner, rules, list, text, index, topLevel) {
            var len = rules.length;
            if (index === undefined) index = 0;
            index = Number(index);
            if (isNaN(index)) index = 0;
            if (index < 0 || index > len)
                throw domError('IndexSizeError',
                    'insertRule index ' + index + ' out of range');
            var rule = parseOne(text, owner.__parentStyleSheet || owner,
                                topLevel ? null : owner);
            if (!rule)
                throw domError('SyntaxError', 'failed to parse rule');
            if (!topLevel && (rule.__at === 'import' || rule.__at === 'namespace'))
                throw domError('HierarchyRequestError',
                    '@' + rule.__at + ' not allowed here');
            rules.splice(index, 0, rule);
            syncList(list, rules);
            notify(owner);
            return index;
        }
        function deleteFrom(owner, rules, list, index) {
            index = Number(index) || 0;
            if (index < 0 || index >= rules.length)
                throw domError('IndexSizeError',
                    'deleteRule index ' + index + ' out of range');
            var removed = rules[index];
            rules.splice(index, 1);
            removed.__parentStyleSheet = null;
            removed.__parentRule = null;
            syncList(list, rules);
            notify(owner);
        }

        var SheetProto = (typeof global.CSSStyleSheet === 'function' &&
                          global.CSSStyleSheet.prototype) || Object.prototype;
        var nativeConstructedReplaceSync = SheetProto.replaceSync;

        function constructedState(sheet) {
            if (sheet.__ndConstructedState) return sheet.__ndConstructedState;
            var rules = parseRuleList(sheet.__cssText || '', sheet, null)
                .filter(function (rule) { return rule.__at !== 'import'; });
            var list = makeList();
            syncList(list, rules);
            var state = { rules: rules, list: list };
            Object.defineProperty(sheet, '__ndConstructedState', {
                value: state, configurable: true
            });
            return state;
        }
        function commitConstructed(sheet) {
            var state = constructedState(sheet);
            var text = state.rules.map(function (rule) {
                return rule.cssText;
            }).join('\n');
            if (nativeConstructedReplaceSync)
                nativeConstructedReplaceSync.call(sheet, text);
            else
                sheet.__cssText = text;
        }
        Object.defineProperties(SheetProto, {
            media: {
                configurable: true, enumerable: true,
                get: function () {
                    return makeMediaListObject(this.__mediaText || '');
                }
            },
            cssRules: {
                configurable: true,
                get: function () { return constructedState(this).list; }
            },
            rules: {
                configurable: true,
                get: function () { return constructedState(this).list; }
            },
            insertRule: {
                configurable: true, writable: true,
                value: function (text, index) {
                    if (arguments.length === 0)
                        throw new TypeError('insertRule requires a rule');
                    if (/^\s*@import\b/i.test(String(text)))
                        throw domError('SyntaxError',
                                       '@import is not allowed here');
                    var state = constructedState(this);
                    return insertInto(this, state.rules, state.list,
                                      text, index, true);
                }
            },
            deleteRule: {
                configurable: true, writable: true,
                value: function (index) {
                    if (arguments.length === 0)
                        throw new TypeError('deleteRule requires an index');
                    var state = constructedState(this);
                    return deleteFrom(this, state.rules, state.list, index);
                }
            },
            addRule: {
                configurable: true, writable: true,
                value: function (selector, block, index) {
                    selector = arguments.length > 0 ? String(selector)
                                                    : 'undefined';
                    block = arguments.length > 1 ? String(block)
                                                 : 'undefined';
                    var rule = selector + ' { ' +
                        (block ? block + ' ' : '') + '}';
                    if (arguments.length < 3)
                        index = this.cssRules.length;
                    this.insertRule(rule, index);
                    return -1;
                }
            },
            removeRule: {
                configurable: true, writable: true,
                value: function (index) {
                    if (arguments.length === 0) index = 0;
                    var state = constructedState(this);
                    return deleteFrom(this, state.rules, state.list, index);
                }
            },
            replaceSync: {
                configurable: true, writable: true,
                value: function (text) {
                    var state = constructedState(this);
                    state.rules = parseRuleList(
                        String(text == null ? '' : text), this, null)
                        .filter(function (rule) {
                            return rule.__at !== 'import';
                        });
                    syncList(state.list, state.rules);
                    commitConstructed(this);
                }
            },
            replace: {
                configurable: true, writable: true,
                value: function (text) {
                    try {
                        this.replaceSync(text);
                        return Promise.resolve(this);
                    } catch (e) {
                        return Promise.reject(e);
                    }
                }
            },
            __notify: {
                configurable: true,
                value: function () { commitConstructed(this); }
            }
        });

        function sheetFor(node) {
            if (node.__ndSheet) return node.__ndSheet;
            var sheet = Object.create(SheetProto);
            var rules = null, list = makeList(), lastText = null;
            var isLink = node.tagName &&
                         node.tagName.toLowerCase() === 'link';

            function sourceText() {
                if (!isLink) {
                    try { return node.textContent || ''; } catch (e) { return ''; }
                }
                if (typeof global.__ns_linked_css !== 'function') return '';
                try { return global.__ns_linked_css(node.href || '') || ''; }
                catch (e) { return ''; }
            }

            function ensure() {
                var txt = sourceText();
                if (rules !== null && txt === lastText) return;
                lastText = txt;
                rules = parseRuleList(txt, sheet, null);
                syncList(list, rules);
            }
            function rebuild() {
                try {
                    var t = rules.map(function (r) {
                        return r.cssText;
                    }).join('\n');
                    lastText = t;
                    node.textContent = t;
                } catch (e) {}
            }

            Object.defineProperties(sheet, {
                ownerNode: { value: node, enumerable: true },
                ownerRule: { value: null, enumerable: true },
                parentStyleSheet: { value: null, enumerable: true },
                type: { value: 'text/css', enumerable: true },
                href: {
                    enumerable: true, configurable: true,
                    get: function () { return isLink ? (node.href || null) : null; }
                },
                title: {
                    value: (node.getAttribute &&
                            node.getAttribute('title')) || null,
                    enumerable: true
                },
                media: {
                    enumerable: true, configurable: true,
                    get: function () {
                        return makeMediaListObject(
                            (node.getAttribute &&
                             node.getAttribute('media')) || '',
                            function (text) {
                                try { node.setAttribute('media', text); }
                                catch (e) {}
                            });
                    }
                },
                disabled: {
                    enumerable: true, configurable: true,
                    get: function () { return !!node.disabled; },
                    set: function (v) { node.disabled = !!v; }
                },
                cssRules: {
                    enumerable: true, configurable: true,
                    get: function () { ensure(); return list; }
                },
                rules: {
                    enumerable: true, configurable: true,
                    get: function () { ensure(); return list; }
                },
                insertRule: {
                    enumerable: true, configurable: true, writable: true,
                    value: function (text, index) {
                        if (arguments.length === 0)
                            throw new TypeError('insertRule requires a rule');
                        ensure();
                        return insertInto(sheet, rules, list, text, index, true);
                    }
                },
                deleteRule: {
                    enumerable: true, configurable: true, writable: true,
                    value: function (index) {
                        if (arguments.length === 0)
                            throw new TypeError('deleteRule requires an index');
                        ensure();
                        return deleteFrom(sheet, rules, list, index);
                    }
                },
                addRule: {
                    enumerable: true, configurable: true, writable: true,
                    value: function (selector, block, index) {
                        selector = arguments.length > 0 ? String(selector)
                                                        : 'undefined';
                        block = arguments.length > 1 ? String(block)
                                                     : 'undefined';
                        ensure();
                        var rule = selector + ' { ' +
                            (block ? block + ' ' : '') + '}';
                        if (arguments.length < 3) index = rules.length;
                        insertInto(sheet, rules, list, rule, index, true);
                        return -1;
                    }
                },
                removeRule: {
                    enumerable: true, configurable: true, writable: true,
                    value: function (index) {
                        ensure();
                        if (arguments.length === 0) index = 0;
                        return deleteFrom(sheet, rules, list, index);
                    }
                },
                replaceSync: {
                    enumerable: true, configurable: true, writable: true,
                    value: function () {
                        throw domError('NotAllowedError',
                                       'stylesheet is not constructed');
                    }
                },
                replace: {
                    enumerable: true, configurable: true, writable: true,
                    value: function () {
                        return Promise.reject(domError(
                            'NotAllowedError', 'stylesheet is not constructed'));
                    }
                },
                __notify: {
                    configurable: true,
                    value: function () { ensure(); rebuild(); }
                }
            });

            try {
                Object.defineProperty(node, '__ndSheet', {
                    value: sheet, writable: true,
                    configurable: true, enumerable: false
                });
            } catch (e) { node.__ndSheet = sheet; }
            return sheet;
        }

        function defSheet(proto) {
            if (!proto) return;
            try {
                Object.defineProperty(proto, 'sheet', {
                    configurable: true,
                    get: function () {
                        var nm = this.tagName ? this.tagName.toLowerCase() : '';
                        if (nm === 'style') return sheetFor(this);
                        if (nm === 'link') {
                            var rel = (this.getAttribute &&
                                       this.getAttribute('rel') || '').toLowerCase();
                            if (rel && rel.indexOf('stylesheet') < 0) return null;
                            return sheetFor(this);
                        }
                        return null;
                    }
                });
            } catch (e) {}
        }
        if (global.Element && global.Element.prototype)
            defSheet(global.Element.prototype);
        else {
            defSheet(global.HTMLStyleElement && global.HTMLStyleElement.prototype);
            defSheet(global.HTMLLinkElement && global.HTMLLinkElement.prototype);
        }

        try {
            var styleSheetsDef = {
                configurable: true,
                get: function () {
                    var self = this || document;
                    var nodes;
                    if (self.host && !self.host.isConnected) nodes = [];
                    else {
                        try {
                            nodes = self.querySelectorAll(
                                'style, link[rel~="stylesheet"]');
                        } catch (e) {
                            try { nodes = self.getElementsByTagName('style'); }
                            catch (e2) { nodes = []; }
                        }
                    }
                    var slist = [];
                    for (var i = 0; i < nodes.length; i++) {
                        var s = sheetFor(nodes[i]);
                        if (s) slist.push(s);
                    }
                    slist.item = function (i) { return this[i] || null; };
                    return slist;
                }
            };
            if (global.Document && global.Document.prototype)
                Object.defineProperty(global.Document.prototype,
                                      'styleSheets', styleSheetsDef);
            if (global.ShadowRoot && global.ShadowRoot.prototype)
                Object.defineProperty(global.ShadowRoot.prototype,
                                      'styleSheets', styleSheetsDef);
            Object.defineProperty(document, 'styleSheets', styleSheetsDef);
        } catch (e) {}
    })();

    /* Advertise the spec-required IntersectionObserverEntry/ResizeObserverEntry
     * prototype members so feature-detecting polyfills (e.g. the
     * "intersection-observer" npm package, which checks 'intersectionRatio' in
     * IntersectionObserverEntry.prototype) detect native support and do NOT
     * replace our working native observers with a JS polyfill that breaks in
     * this engine (Reddit's feed loader relies on a working observer). */
    (function () {
        function ensureProto(ctorName, props) {
            var C = global[ctorName];
            if (typeof C !== 'function' || !C.prototype) return;
            var p = C.prototype, k;
            for (k in props) {
                if (!(k in p)) {
                    try {
                        Object.defineProperty(p, k, {
                            value: props[k], writable: true,
                            configurable: true, enumerable: false
                        });
                    } catch (e) {}
                }
            }
        }
        ensureProto('IntersectionObserverEntry', {
            time: 0, rootBounds: null, boundingClientRect: null,
            intersectionRect: null, isIntersecting: false,
            intersectionRatio: 0, target: null
        });
        ensureProto('ResizeObserverEntry', {
            target: null, contentRect: null,
            borderBoxSize: undefined, contentBoxSize: undefined,
            devicePixelContentBoxSize: undefined
        });
    })();

    (function () {
        var doc = global.document;
        if (!doc || typeof doc.createElement !== 'function') return;

        function domEx(name) {
            try { return new DOMException(name, name); }
            catch (e) {
                var err = new Error(name);
                err.name = name;
                return err;
            }
        }

        function rangeEx(code) {
            var err = new Error('RangeException ' + code);
            err.name = 'RangeException';
            err.code = code;
            err.BAD_BOUNDARYPOINTS_ERR = 1;
            err.INVALID_NODE_TYPE_ERR = 2;
            return err;
        }

        function isCharData(n) {
            var t = n.nodeType;
            return t === 3 || t === 4 || t === 7 || t === 8;
        }

        function isTextNode(n) {
            return n.nodeType === 3 || n.nodeType === 4;
        }

        function nodeLength(n) {
            if (n.nodeType === 10 || n.nodeType === 2) return 0;
            if (isCharData(n)) return n.data ? n.data.length : 0;
            return n.childNodes ? n.childNodes.length : 0;
        }

        function indexOfNode(n) {
            var i = 0;
            while ((n = n.previousSibling)) i++;
            return i;
        }

        function rootOf(n) {
            while (n.parentNode) n = n.parentNode;
            return n;
        }

        function ownerDoc(n) {
            if (n.nodeType === 9) return n;
            return n.ownerDocument || doc;
        }

        function isInclusiveAncestor(a, b) {
            while (b) {
                if (b === a) return true;
                b = b.parentNode;
            }
            return false;
        }

        function pathTo(n) {
            var p = [];
            while (n) { p.unshift(n); n = n.parentNode; }
            return p;
        }

        function bpCompare(nodeA, offsetA, nodeB, offsetB) {
            if (nodeA === nodeB)
                return offsetA < offsetB ? -1 : offsetA > offsetB ? 1 : 0;
            var pa = pathTo(nodeA), pb = pathTo(nodeB);
            var i = 0;
            while (i < pa.length && i < pb.length && pa[i] === pb[i]) i++;
            if (i === pa.length)
                return indexOfNode(pb[i]) < offsetA ? 1 : -1;
            if (i === pb.length)
                return indexOfNode(pa[i]) < offsetB ? -1 : 1;
            return indexOfNode(pa[i]) < indexOfNode(pb[i]) ? -1 : 1;
        }

        function replaceData(n, off, count, s) {
            if (typeof n.replaceData === 'function') {
                n.replaceData(off, count, s);
                return;
            }
            var d = n.data || '';
            n.data = d.substring(0, off) + s + d.substring(off + count);
        }

        function checkBoundary(node, offset) {
            if (!node || typeof node.nodeType !== 'number')
                throw new TypeError('parameter 1 is not of type Node');
            if (node.nodeType === 10) throw domEx('InvalidNodeTypeError');
            offset = Number(offset) >>> 0;
            if (offset > nodeLength(node)) throw domEx('IndexSizeError');
            return offset;
        }

        var liveRanges = [];
        var canTrack = typeof WeakRef === 'function';

        function trackRange(r) {
            if (canTrack) liveRanges.push(new WeakRef(r));
            return r;
        }

        function forEachLiveRange(cb) {
            if (!canTrack) return;
            var kept = 0;
            for (var i = 0; i < liveRanges.length; i++) {
                var r = liveRanges[i].deref();
                if (r === undefined) continue;
                liveRanges[kept++] = liveRanges[i];
                cb(r);
            }
            liveRanges.length = kept;
        }

        function rangeReplaceData(node, offset, count, newLength) {
            var delta = newLength - count;
            forEachLiveRange(function (r) {
                var o;
                if (r._sc === node) {
                    o = r._so;
                    if (o > offset && o <= offset + count) r._so = offset;
                    else if (o > offset + count) r._so = o + delta;
                }
                if (r._ec === node) {
                    o = r._eo;
                    if (o > offset && o <= offset + count) r._eo = offset;
                    else if (o > offset + count) r._eo = o + delta;
                }
            });
        }

        function NdRange(ownerDoc) {
            var d = ownerDoc || doc;
            this._sc = d; this._so = 0;
            this._ec = d; this._eo = 0;
            trackRange(this);
        }

        function mkRange(sc, so, ec, eo) {
            var r = new NdRange();
            r._sc = sc; r._so = so; r._ec = ec; r._eo = eo;
            return r;
        }

        function containedIn(n, r) {
            return rootOf(n) === rootOf(r._sc) &&
                   bpCompare(n, 0, r._sc, r._so) > 0 &&
                   bpCompare(n, nodeLength(n), r._ec, r._eo) < 0;
        }

        function partiallyContainedIn(n, r) {
            var a = isInclusiveAncestor(n, r._sc);
            var b = isInclusiveAncestor(n, r._ec);
            return (a && !b) || (b && !a);
        }

        Object.defineProperties(NdRange.prototype, {
            startContainer: { get: function () { return this._sc; }, configurable: true },
            startOffset:    { get: function () { return this._so; }, configurable: true },
            endContainer:   { get: function () { return this._ec; }, configurable: true },
            endOffset:      { get: function () { return this._eo; }, configurable: true },
            collapsed:      { get: function () {
                return this._sc === this._ec && this._so === this._eo;
            }, configurable: true },
            commonAncestorContainer: { get: function () {
                for (var a = this._sc; a; a = a.parentNode)
                    if (isInclusiveAncestor(a, this._ec)) return a;
                return null;
            }, configurable: true }
        });

        NdRange.prototype.setStart = function (node, offset) {
            offset = checkBoundary(node, offset);
            this._sc = node; this._so = offset;
            if (rootOf(node) !== rootOf(this._ec) ||
                bpCompare(node, offset, this._ec, this._eo) > 0) {
                this._ec = node; this._eo = offset;
            }
        };

        NdRange.prototype.setEnd = function (node, offset) {
            offset = checkBoundary(node, offset);
            this._ec = node; this._eo = offset;
            if (rootOf(node) !== rootOf(this._sc) ||
                bpCompare(node, offset, this._sc, this._so) < 0) {
                this._sc = node; this._so = offset;
            }
        };

        NdRange.prototype.setStartBefore = function (node) {
            if (!node || typeof node.nodeType !== 'number')
                throw new TypeError('parameter 1 is not of type Node');
            var parent = node.parentNode;
            if (!parent) throw domEx('InvalidNodeTypeError');
            this.setStart(parent, indexOfNode(node));
        };

        NdRange.prototype.setStartAfter = function (node) {
            if (!node || typeof node.nodeType !== 'number')
                throw new TypeError('parameter 1 is not of type Node');
            var parent = node.parentNode;
            if (!parent) throw domEx('InvalidNodeTypeError');
            this.setStart(parent, indexOfNode(node) + 1);
        };

        NdRange.prototype.setEndBefore = function (node) {
            if (!node || typeof node.nodeType !== 'number')
                throw new TypeError('parameter 1 is not of type Node');
            var parent = node.parentNode;
            if (!parent) throw domEx('InvalidNodeTypeError');
            this.setEnd(parent, indexOfNode(node));
        };

        NdRange.prototype.setEndAfter = function (node) {
            if (!node || typeof node.nodeType !== 'number')
                throw new TypeError('parameter 1 is not of type Node');
            var parent = node.parentNode;
            if (!parent) throw domEx('InvalidNodeTypeError');
            this.setEnd(parent, indexOfNode(node) + 1);
        };

        NdRange.prototype.collapse = function (toStart) {
            if (toStart) { this._ec = this._sc; this._eo = this._so; }
            else { this._sc = this._ec; this._so = this._eo; }
        };

        NdRange.prototype.selectNode = function (node) {
            if (!node || typeof node.nodeType !== 'number')
                throw new TypeError('parameter 1 is not of type Node');
            var parent = node.parentNode;
            if (!parent) throw domEx('InvalidNodeTypeError');
            var i = indexOfNode(node);
            this._sc = parent; this._so = i;
            this._ec = parent; this._eo = i + 1;
        };

        NdRange.prototype.selectNodeContents = function (node) {
            if (!node || typeof node.nodeType !== 'number')
                throw new TypeError('parameter 1 is not of type Node');
            if (node.nodeType === 10) throw domEx('InvalidNodeTypeError');
            this._sc = node; this._so = 0;
            this._ec = node; this._eo = nodeLength(node);
        };

        NdRange.prototype.cloneRange = function () {
            return mkRange(this._sc, this._so, this._ec, this._eo);
        };

        NdRange.prototype.detach = function () {};

        NdRange.prototype.comparePoint = function (node, offset) {
            if (!node || typeof node.nodeType !== 'number')
                throw new TypeError('parameter 1 is not of type Node');
            if (rootOf(node) !== rootOf(this._sc))
                throw domEx('WrongDocumentError');
            if (node.nodeType === 10) throw domEx('InvalidNodeTypeError');
            offset = Number(offset) >>> 0;
            if (offset > nodeLength(node)) throw domEx('IndexSizeError');
            if (bpCompare(node, offset, this._sc, this._so) < 0) return -1;
            if (bpCompare(node, offset, this._ec, this._eo) > 0) return 1;
            return 0;
        };

        NdRange.prototype.isPointInRange = function (node, offset) {
            if (!node || typeof node.nodeType !== 'number')
                throw new TypeError('parameter 1 is not of type Node');
            if (rootOf(node) !== rootOf(this._sc)) return false;
            if (node.nodeType === 10) throw domEx('InvalidNodeTypeError');
            offset = Number(offset) >>> 0;
            if (offset > nodeLength(node)) throw domEx('IndexSizeError');
            return bpCompare(node, offset, this._sc, this._so) >= 0 &&
                   bpCompare(node, offset, this._ec, this._eo) <= 0;
        };

        NdRange.prototype.intersectsNode = function (node) {
            if (!node || typeof node.nodeType !== 'number')
                throw new TypeError('parameter 1 is not of type Node');
            if (rootOf(node) !== rootOf(this._sc)) return false;
            var parent = node.parentNode;
            if (!parent) return true;
            var offset = indexOfNode(node);
            return bpCompare(parent, offset, this._ec, this._eo) < 0 &&
                   bpCompare(parent, offset + 1, this._sc, this._so) > 0;
        };

        NdRange.prototype.compareBoundaryPoints = function (how, sourceRange) {
            how = (how >>> 0) & 0xffff;
            if (!sourceRange || !(sourceRange instanceof NdRange))
                throw new TypeError('parameter 2 is not of type Range');
            if (how > 3) throw domEx('NotSupportedError');
            if (rootOf(this._sc) !== rootOf(sourceRange._sc))
                throw domEx('WrongDocumentError');
            var tn, to, on, oo;
            if (how === 0) { tn = this._sc; to = this._so; on = sourceRange._sc; oo = sourceRange._so; }
            else if (how === 1) { tn = this._ec; to = this._eo; on = sourceRange._sc; oo = sourceRange._so; }
            else if (how === 2) { tn = this._ec; to = this._eo; on = sourceRange._ec; oo = sourceRange._eo; }
            else { tn = this._sc; to = this._so; on = sourceRange._ec; oo = sourceRange._eo; }
            return bpCompare(tn, to, on, oo);
        };

        NdRange.prototype.toString = function () {
            var sc = this._sc, so = this._so, ec = this._ec, eo = this._eo;
            if (sc === ec && isTextNode(sc))
                return (sc.data || '').substring(so, eo);
            if (sc === ec && sc.nodeType === 2) {
                var av = '', akids = sc.childNodes || [];
                for (var ai = so; ai < eo && ai < akids.length; ai++)
                    if (isTextNode(akids[ai])) av += akids[ai].data || '';
                return av;
            }
            var s = '';
            if (isTextNode(sc)) s += (sc.data || '').substring(so);
            var self = this;
            (function walk(n) {
                for (var c = n.firstChild; c; c = c.nextSibling) {
                    if (isTextNode(c) && containedIn(c, self)) s += c.data || '';
                    walk(c);
                }
            })(this.commonAncestorContainer || rootOf(sc));
            if (isTextNode(ec) && ec !== sc) s += (ec.data || '').substring(0, eo);
            return s;
        };

        function collectContainedRoots(r) {
            var out = [];
            (function walk(n) {
                for (var c = n.firstChild; c; c = c.nextSibling) {
                    if (containedIn(c, r)) { out.push(c); continue; }
                    walk(c);
                }
            })(r.commonAncestorContainer || rootOf(r._sc));
            return out;
        }

        function cloneOrExtract(r, extract) {
            var sc = r._sc, so = r._so, ec = r._ec, eo = r._eo;
            var frag = ownerDoc(sc).createDocumentFragment();
            if (r.collapsed) return frag;
            if (sc === ec && sc.nodeType === 2) {
                var akids = sc.childNodes || [];
                for (var ai = so; ai < eo && ai < akids.length; ai++) {
                    if (extract) frag.appendChild(akids[ai]);
                    else frag.appendChild(akids[ai].cloneNode(true));
                }
                if (extract) {
                    sc.value = '';
                    r._sc = sc; r._so = 0; r._ec = sc; r._eo = 0;
                }
                return frag;
            }
            if (sc === ec && isCharData(sc)) {
                var c0 = sc.cloneNode(false);
                c0.data = (sc.data || '').substring(so, eo);
                frag.appendChild(c0);
                if (extract) replaceData(sc, so, eo - so, '');
                return frag;
            }
            var commonAncestor = r.commonAncestorContainer;
            var firstPartial = null, lastPartial = null;
            if (!isInclusiveAncestor(sc, ec))
                for (var f = commonAncestor.firstChild; f; f = f.nextSibling)
                    if (partiallyContainedIn(f, r)) { firstPartial = f; break; }
            if (!isInclusiveAncestor(ec, sc))
                for (var l = commonAncestor.lastChild; l; l = l.previousSibling)
                    if (partiallyContainedIn(l, r)) { lastPartial = l; break; }
            var contained = [];
            for (var ch = commonAncestor.firstChild; ch; ch = ch.nextSibling)
                if (containedIn(ch, r)) {
                    if (ch.nodeType === 10) throw domEx('HierarchyRequestError');
                    contained.push(ch);
                }
            var newNode = null, newOffset = 0;
            if (extract) {
                if (isInclusiveAncestor(sc, ec)) {
                    newNode = sc; newOffset = so;
                } else {
                    var ref = sc;
                    while (ref.parentNode &&
                           !isInclusiveAncestor(ref.parentNode, ec))
                        ref = ref.parentNode;
                    newNode = ref.parentNode;
                    newOffset = indexOfNode(ref) + 1;
                }
            }
            if (firstPartial && isCharData(firstPartial)) {
                var c1 = sc.cloneNode(false);
                c1.data = (sc.data || '').substring(so);
                frag.appendChild(c1);
                if (extract) replaceData(sc, so, nodeLength(sc) - so, '');
            } else if (firstPartial) {
                var c2 = firstPartial.cloneNode(false);
                frag.appendChild(c2);
                var sub1 = mkRange(sc, so, firstPartial, nodeLength(firstPartial));
                c2.appendChild(cloneOrExtract(sub1, extract));
            }
            for (var i = 0; i < contained.length; i++) {
                if (extract) frag.appendChild(contained[i]);
                else frag.appendChild(contained[i].cloneNode(true));
            }
            if (lastPartial && isCharData(lastPartial)) {
                var c3 = ec.cloneNode(false);
                c3.data = (ec.data || '').substring(0, eo);
                frag.appendChild(c3);
                if (extract) replaceData(ec, 0, eo, '');
            } else if (lastPartial) {
                var c4 = lastPartial.cloneNode(false);
                frag.appendChild(c4);
                var sub2 = mkRange(lastPartial, 0, ec, eo);
                c4.appendChild(cloneOrExtract(sub2, extract));
            }
            if (extract) {
                r._sc = newNode; r._so = newOffset;
                r._ec = newNode; r._eo = newOffset;
            }
            return frag;
        }

        NdRange.prototype.cloneContents = function () {
            return cloneOrExtract(this, false);
        };

        NdRange.prototype.extractContents = function () {
            return cloneOrExtract(this, true);
        };

        NdRange.prototype.deleteContents = function () {
            if (this.collapsed) return;
            var sc = this._sc, so = this._so, ec = this._ec, eo = this._eo;
            if (sc === ec && isCharData(sc)) {
                replaceData(sc, so, eo - so, '');
                return;
            }
            var toRemove = collectContainedRoots(this);
            var newNode, newOffset;
            if (isInclusiveAncestor(sc, ec)) {
                newNode = sc; newOffset = so;
            } else {
                var ref = sc;
                while (ref.parentNode && !isInclusiveAncestor(ref.parentNode, ec))
                    ref = ref.parentNode;
                newNode = ref.parentNode;
                newOffset = indexOfNode(ref) + 1;
            }
            if (isCharData(sc)) replaceData(sc, so, nodeLength(sc) - so, '');
            for (var i = 0; i < toRemove.length; i++)
                if (toRemove[i].parentNode)
                    toRemove[i].parentNode.removeChild(toRemove[i]);
            if (isCharData(ec)) replaceData(ec, 0, eo, '');
            this._sc = newNode; this._so = newOffset;
            this._ec = newNode; this._eo = newOffset;
        };

        function countElementChildren(p) {
            var n = 0;
            for (var c = p.firstChild; c; c = c.nextSibling)
                if (c.nodeType === 1) n++;
            return n;
        }
        function hasChildOfType(p, t) {
            for (var c = p.firstChild; c; c = c.nextSibling)
                if (c.nodeType === t) return true;
            return false;
        }
        function doctypeFollowing(child) {
            for (var c = child.nextSibling; c; c = c.nextSibling)
                if (c.nodeType === 10) return true;
            return false;
        }
        function elementPreceding(child) {
            for (var c = child.previousSibling; c; c = c.previousSibling)
                if (c.nodeType === 1) return true;
            return false;
        }

        function ensurePreInsertion(node, parent, child) {
            var pt = parent.nodeType;
            if (pt !== 1 && pt !== 9 && pt !== 11)
                throw domEx('HierarchyRequestError');
            if (isInclusiveAncestor(node, parent))
                throw domEx('HierarchyRequestError');
            if (child && child.parentNode !== parent)
                throw domEx('NotFoundError');
            var nt = node.nodeType;
            if (nt !== 1 && nt !== 3 && nt !== 4 && nt !== 7 && nt !== 8 &&
                nt !== 10 && nt !== 11)
                throw domEx('HierarchyRequestError');
            if ((nt === 3 || nt === 4) && pt === 9)
                throw domEx('HierarchyRequestError');
            if (nt === 10 && pt !== 9)
                throw domEx('HierarchyRequestError');
            if (pt !== 9) return;
            if (nt === 11) {
                var elems = countElementChildren(node);
                if (elems > 1 || hasChildOfType(node, 3))
                    throw domEx('HierarchyRequestError');
                if (elems === 1 && (countElementChildren(parent) > 0 ||
                    (child && child.nodeType === 10) ||
                    (child && doctypeFollowing(child))))
                    throw domEx('HierarchyRequestError');
            } else if (nt === 1) {
                if (countElementChildren(parent) > 0 ||
                    (child && child.nodeType === 10) ||
                    (child && doctypeFollowing(child)))
                    throw domEx('HierarchyRequestError');
            } else if (nt === 10) {
                if (hasChildOfType(parent, 10) ||
                    (child && elementPreceding(child)) ||
                    (!child && countElementChildren(parent) > 0))
                    throw domEx('HierarchyRequestError');
            }
        }

        NdRange.prototype.insertNode = function (node) {
            if (!node || typeof node.nodeType !== 'number')
                throw new TypeError('parameter 1 is not of type Node');
            var sc = this._sc, so = this._so;
            var sct = sc.nodeType;
            if (sct === 7 || sct === 8 ||
                (isTextNode(sc) && !sc.parentNode) || node === sc)
                throw domEx('HierarchyRequestError');
            var referenceNode;
            if (isTextNode(sc)) referenceNode = sc;
            else referenceNode = (sct !== 2 && sc.childNodes &&
                                  sc.childNodes[so]) || null;
            var parent = referenceNode ? referenceNode.parentNode : sc;
            ensurePreInsertion(node, parent, referenceNode);
            if (isTextNode(sc)) referenceNode = sc.splitText(so);
            if (node === referenceNode) referenceNode = referenceNode.nextSibling;
            if (node.parentNode) node.parentNode.removeChild(node);
            var newOffset = referenceNode ? indexOfNode(referenceNode)
                                          : nodeLength(parent);
            newOffset += node.nodeType === 11 ? nodeLength(node) : 1;
            var wasCollapsed = this.collapsed;
            parent.insertBefore(node, referenceNode);
            if (wasCollapsed) { this._ec = parent; this._eo = newOffset; }
        };

        NdRange.prototype.surroundContents = function (newParent) {
            if (!newParent || typeof newParent.nodeType !== 'number')
                throw new TypeError('parameter 1 is not of type Node');
            for (var a = this._sc; a && !isInclusiveAncestor(a, this._ec);
                 a = a.parentNode)
                if (!isTextNode(a)) throw domEx('InvalidStateError');
            for (var b = this._ec; b && !isInclusiveAncestor(b, this._sc);
                 b = b.parentNode)
                if (!isTextNode(b)) throw domEx('InvalidStateError');
            var nt = newParent.nodeType;
            if (nt === 9 || nt === 10 || nt === 11)
                throw domEx('InvalidNodeTypeError');
            var fragment = this.extractContents();
            while (newParent.firstChild)
                newParent.removeChild(newParent.firstChild);
            this.insertNode(newParent);
            newParent.appendChild(fragment);
            this.selectNode(newParent);
        };

        NdRange.prototype.createContextualFragment = function (html) {
            var node = this._sc;
            var el = node.nodeType === 1 ? node
                   : isCharData(node) ? node.parentNode : null;
            var ns = el && el.nodeType === 1 ? el.namespaceURI : null;
            var foreign = ns === 'http://www.w3.org/2000/svg' ||
                          ns === 'http://www.w3.org/1998/Math/MathML';
            var tag = el && el.nodeType === 1 ? el.localName : 'body';
            if (tag === 'html' && !foreign) tag = 'body';
            var scratch;
            try {
                scratch = foreign ? doc.createElementNS(ns, tag)
                                  : doc.createElement(tag);
            } catch (e) { scratch = doc.createElement('body'); }
            scratch.innerHTML = String(html);
            var frag = ownerDoc(node).createDocumentFragment();
            while (scratch.firstChild) frag.appendChild(scratch.firstChild);
            return frag;
        };

        function nativeProxy(self) {
            if (typeof global.__ndNativeRange !== 'function') return null;
            var r = global.__ndNativeRange();
            r.startContainer = self._sc;
            r.startOffset = self._so;
            r.endContainer = self._ec;
            r.endOffset = self._eo;
            r.collapsed = self.collapsed;
            return r;
        }

        NdRange.prototype.getBoundingClientRect = function () {
            var r = nativeProxy(this);
            return r ? r.getBoundingClientRect() : null;
        };

        NdRange.prototype.getClientRects = function () {
            var r = nativeProxy(this);
            return r ? r.getClientRects() : [];
        };

        var rangeConstants = {
            START_TO_START: 0, START_TO_END: 1,
            END_TO_END: 2, END_TO_START: 3
        };
        for (var rk in rangeConstants) {
            NdRange[rk] = rangeConstants[rk];
            NdRange.prototype[rk] = rangeConstants[rk];
        }

        (function wrapCharacterDataMutations() {
            var tn;
            try { tn = doc.createTextNode('x'); } catch (e) { return; }

            function ownerProtoWith(obj, prop) {
                var p = obj;
                while (p) {
                    if (Object.prototype.hasOwnProperty.call(p, prop)) return p;
                    p = Object.getPrototypeOf(p);
                }
                return null;
            }
            function clamp(v, lo, hi) {
                v = Math.trunc(Number(v));
                if (!isFinite(v)) v = 0;
                if (v < lo) v = lo;
                if (v > hi) v = hi;
                return v;
            }
            function clampU(v, hi) {
                v = Number(v);
                if (!isFinite(v)) v = 0;
                v = v >>> 0;
                return v > hi ? hi : v;
            }
            function curLen(node) {
                return node.data ? node.data.length : 0;
            }
            function strLen(v) {
                return (v === null || v === undefined ? '' : String(v)).length;
            }

            function wrapMethod(name, plan) {
                var proto = ownerProtoWith(tn, name);
                if (!proto) return;
                var orig = proto[name];
                if (typeof orig !== 'function') return;
                var fn = function () {
                    var len = curLen(this);
                    var ret = orig.apply(this, arguments);
                    var p = plan(len, arguments);
                    if (p) rangeReplaceData(this, p[0], p[1], p[2]);
                    return ret;
                };
                try { Object.defineProperty(fn, 'length', { value: orig.length, configurable: true }); } catch (e) {}
                try { Object.defineProperty(fn, 'name', { value: name, configurable: true }); } catch (e) {}
                Object.defineProperty(proto, name, {
                    value: fn, writable: true, configurable: true, enumerable: false
                });
            }

            wrapMethod('replaceData', function (len, args) {
                var off = clampU(args[0], len);
                return [off, clampU(args[1], len - off), strLen(args[2])];
            });
            wrapMethod('insertData', function (len, args) {
                return [clampU(args[0], len), 0, strLen(args[1])];
            });
            wrapMethod('deleteData', function (len, args) {
                var off = clampU(args[0], len);
                return [off, clampU(args[1], len - off), 0];
            });

            function wrapAccessor(name) {
                var proto = ownerProtoWith(tn, name);
                if (!proto) return;
                var d = Object.getOwnPropertyDescriptor(proto, name);
                if (!d || typeof d.set !== 'function') return;
                var origSet = d.set, origGet = d.get;
                Object.defineProperty(proto, name, {
                    get: origGet,
                    set: function (v) {
                        if (!isCharData(this)) { origSet.call(this, v); return; }
                        var len = curLen(this);
                        origSet.call(this, v);
                        rangeReplaceData(this, 0, len, strLen(v));
                    },
                    configurable: true, enumerable: d.enumerable === true
                });
            }

            wrapAccessor('data');
            wrapAccessor('nodeValue');
            wrapAccessor('textContent');

            var splitProto = ownerProtoWith(tn, 'splitText');
            if (splitProto && typeof splitProto.splitText === 'function') {
                var origSplit = splitProto.splitText;
                Object.defineProperty(splitProto, 'splitText', {
                    value: function (rawOffset) {
                        var len = curLen(this);
                        var offset = clamp(rawOffset, 0, len);
                        var count = len - offset;
                        var parent = this.parentNode;
                        var index = parent ? indexOfNode(this) : 0;
                        var node = this;
                        var newNode = origSplit.apply(this, arguments);
                        if (parent && newNode) {
                            forEachLiveRange(function (r) {
                                if (r._sc === node && r._so > offset) {
                                    r._sc = newNode; r._so -= offset;
                                } else if (r._sc === parent && r._so === index + 1) {
                                    r._so += 1;
                                }
                                if (r._ec === node && r._eo > offset) {
                                    r._ec = newNode; r._eo -= offset;
                                } else if (r._ec === parent && r._eo === index + 1) {
                                    r._eo += 1;
                                }
                            });
                        }
                        rangeReplaceData(node, offset, count, 0);
                        return newNode;
                    },
                    writable: true, configurable: true, enumerable: false
                });
            }
        })();

        (function wrapNodeMutations() {
            var el;
            try { el = doc.createElement('span'); } catch (e) { return; }

            function nodeProtosWith(name) {
                var out = [];
                function scan(start) {
                    var p = start;
                    while (p) {
                        if (Object.prototype.hasOwnProperty.call(p, name)) {
                            if (out.indexOf(p) < 0) out.push(p);
                            return;
                        }
                        p = Object.getPrototypeOf(p);
                    }
                }
                scan(el);
                scan(doc);
                if (typeof global.Document === 'function' && global.Document.prototype)
                    scan(global.Document.prototype);
                return out;
            }
            function preState(node) {
                var parent = node && node.parentNode;
                if (!parent || liveRanges.length === 0) return null;
                return { node: node, parent: parent, index: indexOfNode(node) };
            }
            function applyRemove(st) {
                if (!st) return;
                forEachLiveRange(function (r) {
                    if (isInclusiveAncestor(st.node, r._sc)) { r._sc = st.parent; r._so = st.index; }
                    else if (r._sc === st.parent && r._so > st.index) r._so -= 1;
                    if (isInclusiveAncestor(st.node, r._ec)) { r._ec = st.parent; r._eo = st.index; }
                    else if (r._ec === st.parent && r._eo > st.index) r._eo -= 1;
                });
            }
            function applyInsert(node) {
                var parent = node && node.parentNode;
                if (!parent || liveRanges.length === 0) return;
                if (node.nextSibling === null) return;
                var index = indexOfNode(node);
                forEachLiveRange(function (r) {
                    if (r._sc === parent && r._so > index) r._so += 1;
                    if (r._ec === parent && r._eo > index) r._eo += 1;
                });
            }
            function wrap(name, handler) {
                var protos = nodeProtosWith(name);
                for (var i = 0; i < protos.length; i++) {
                    (function (proto) {
                        var orig = proto[name];
                        if (typeof orig !== 'function') return;
                        var fn = function () { return handler.call(this, orig, arguments); };
                        try { Object.defineProperty(fn, 'length', { value: orig.length, configurable: true }); } catch (e) {}
                        try { Object.defineProperty(fn, 'name', { value: name, configurable: true }); } catch (e) {}
                        Object.defineProperty(proto, name, {
                            value: fn, writable: true, configurable: true, enumerable: false
                        });
                    })(protos[i]);
                }
            }

            wrap('appendChild', function (orig, args) {
                var st = preState(args[0]);
                var ret = orig.apply(this, args);
                applyRemove(st);
                applyInsert(args[0]);
                return ret;
            });
            wrap('insertBefore', function (orig, args) {
                var st = preState(args[0]);
                var ret = orig.apply(this, args);
                applyRemove(st);
                applyInsert(args[0]);
                return ret;
            });
            wrap('removeChild', function (orig, args) {
                var st = preState(args[0]);
                var ret = orig.apply(this, args);
                applyRemove(st);
                return ret;
            });
            wrap('replaceChild', function (orig, args) {
                var newChild = args[0], oldChild = args[1];
                var stOld = preState(oldChild);
                var stNew = (newChild !== oldChild) ? preState(newChild) : null;
                var ret = orig.apply(this, args);
                applyRemove(stOld);
                applyRemove(stNew);
                applyInsert(newChild);
                return ret;
            });
        })();

        nativeize(NdRange, 'Range');
        try { Object.defineProperty(NdRange, 'length', { value: 0 }); } catch (e) {}
        global.Range = NdRange;
        global.__ndCreateRange = function (ownerDoc) {
            return new NdRange(ownerDoc && ownerDoc.nodeType === 9 ? ownerDoc : doc);
        };

        var nativeGetSelection = global.getSelection;
        function selNative() {
            if (typeof nativeGetSelection !== 'function') return null;
            try { return nativeGetSelection.call(global); } catch (e) { return null; }
        }

        function NdSelection() {
            this._range = null;
            this._direction = 'none';
        }
        Object.defineProperties(NdSelection.prototype, {
            rangeCount: { get: function () {
                if (this._range) return 1;
                var n = selNative(); return n ? (n.rangeCount | 0) : 0;
            }, configurable: true },
            isCollapsed: { get: function () {
                if (this._range) return this._range.collapsed;
                var n = selNative(); return n ? !!n.isCollapsed : true;
            }, configurable: true },
            type: { get: function () {
                if (this._range) return this._range.collapsed ? 'Caret' : 'Range';
                var n = selNative(); return n ? String(n.type) : 'None';
            }, configurable: true },
            anchorNode: { get: function () {
                if (!this._range) { var n = selNative(); return n ? n.anchorNode : null; }
                return this._direction === 'backward'
                    ? this._range.endContainer : this._range.startContainer;
            }, configurable: true },
            anchorOffset: { get: function () {
                if (!this._range) { var n = selNative(); return n ? (n.anchorOffset | 0) : 0; }
                return this._direction === 'backward'
                    ? this._range.endOffset : this._range.startOffset;
            }, configurable: true },
            focusNode: { get: function () {
                if (!this._range) { var n = selNative(); return n ? n.focusNode : null; }
                return this._direction === 'backward'
                    ? this._range.startContainer : this._range.endContainer;
            }, configurable: true },
            focusOffset: { get: function () {
                if (!this._range) { var n = selNative(); return n ? (n.focusOffset | 0) : 0; }
                return this._direction === 'backward'
                    ? this._range.startOffset : this._range.endOffset;
            }, configurable: true }
        });
        NdSelection.prototype.getRangeAt = function (i) {
            if (this._range) { if ((i | 0) !== 0) throw domEx('IndexSizeError'); return this._range; }
            var n = selNative();
            if (n && (i | 0) < (n.rangeCount | 0)) return n.getRangeAt(i);
            throw domEx('IndexSizeError');
        };
        NdSelection.prototype.removeAllRanges = function () {
            this._range = null; this._direction = 'none';
        };
        NdSelection.prototype.empty = NdSelection.prototype.removeAllRanges;
        NdSelection.prototype.addRange = function (range) {
            if (this._range || !range) return;
            this._range = range instanceof NdRange ? range
                : mkRange(range.startContainer, range.startOffset,
                          range.endContainer, range.endOffset);
            this._direction = 'forward';
        };
        NdSelection.prototype.removeRange = function (range) {
            if (this._range === range) this.removeAllRanges();
        };
        NdSelection.prototype.collapse = function (node, offset) {
            if (node == null) { this.removeAllRanges(); return; }
            offset = checkBoundary(node, offset);
            this._range = mkRange(node, offset, node, offset);
            this._direction = 'forward';
        };
        NdSelection.prototype.setPosition = NdSelection.prototype.collapse;
        NdSelection.prototype.collapseToStart = function () {
            if (!this._range) throw domEx('InvalidStateError');
            var sc = this._range.startContainer, so = this._range.startOffset;
            this._range = mkRange(sc, so, sc, so); this._direction = 'forward';
        };
        NdSelection.prototype.collapseToEnd = function () {
            if (!this._range) throw domEx('InvalidStateError');
            var ec = this._range.endContainer, eo = this._range.endOffset;
            this._range = mkRange(ec, eo, ec, eo); this._direction = 'forward';
        };
        NdSelection.prototype.extend = function (node, offset) {
            if (!this._range) throw domEx('InvalidStateError');
            offset = checkBoundary(node, offset);
            var an = this.anchorNode, ao = this.anchorOffset;
            if (rootOf(node) !== rootOf(an)) throw domEx('WrongDocumentError');
            if (bpCompare(an, ao, node, offset) <= 0) {
                this._range = mkRange(an, ao, node, offset); this._direction = 'forward';
            } else {
                this._range = mkRange(node, offset, an, ao); this._direction = 'backward';
            }
        };
        NdSelection.prototype.setBaseAndExtent = function (an, ao, fn, fo) {
            ao = checkBoundary(an, ao);
            fo = checkBoundary(fn, fo);
            if (rootOf(an) !== rootOf(fn)) throw domEx('WrongDocumentError');
            if (bpCompare(an, ao, fn, fo) <= 0) {
                this._range = mkRange(an, ao, fn, fo); this._direction = 'forward';
            } else {
                this._range = mkRange(fn, fo, an, ao); this._direction = 'backward';
            }
        };
        NdSelection.prototype.selectAllChildren = function (node) {
            if (!node) return;
            this.setBaseAndExtent(node, 0, node, node.childNodes.length);
        };
        NdSelection.prototype.containsNode = function (node, allowPartial) {
            if (!this._range || !node) return false;
            return allowPartial ? this._range.intersectsNode(node)
                                : containedIn(node, this._range);
        };
        NdSelection.prototype.deleteFromDocument = function () {
            if (this._range) this._range.deleteContents();
        };
        NdSelection.prototype.modify = function () {};
        NdSelection.prototype.toString = function () {
            if (this._range) return this._range.toString();
            var n = selNative(); return n ? String(n) : '';
        };

        global.Selection = NdSelection;
        var theSelection = new NdSelection();
        function getSelectionImpl() { return theSelection; }
        global.getSelection = getSelectionImpl;
        if (doc) { try { doc.getSelection = getSelectionImpl; } catch (e) {} }
    })();

    /* HTMLImageElement.decode(): the native binding always resolved. Per the
     * HTML spec the promise rejects with an "EncodingError" when the image
     * has no usable source, fails to load, or its document is not fully
     * active, and resolves once a usable source has been decoded. The
     * active-document check is deferred one microtask so a synchronous adopt
     * into an inactive document after the call is observed. */
    (function () {
        if (typeof global.HTMLImageElement !== 'function' ||
            !global.HTMLImageElement.prototype) return;

        try {
            if (typeof global.Image === 'function' &&
                global.Image.prototype !== global.HTMLImageElement.prototype)
                global.Image.prototype = global.HTMLImageElement.prototype;
        } catch (e) {}

        function encodingError() {
            try { return new DOMException('The source image cannot be decoded.',
                                          'EncodingError'); }
            catch (e) {
                var err = new Error('The source image cannot be decoded.');
                err.name = 'EncodingError';
                return err;
            }
        }

        var decodeProto = global.HTMLImageElement.prototype;
        try {
            if (typeof document !== 'undefined' && document.createElement) {
                var p = Object.getPrototypeOf(document.createElement('img'));
                while (p && !Object.prototype.hasOwnProperty.call(p, 'decode'))
                    p = Object.getPrototypeOf(p);
                if (p) decodeProto = p;
            }
        } catch (e) {}

        Object.defineProperty(decodeProto, 'decode', {
            configurable: true, writable: true, enumerable: false,
            value: function () {
                var img = this;
                return new Promise(function (resolve, reject) {
                    function fail() { reject(encodingError()); }
                    Promise.resolve().then(function () {
                        var doc = img.ownerDocument;
                        if (!doc || doc.defaultView == null) { fail(); return; }
                        var src = (img.getAttribute && img.getAttribute('src')) || '';
                        var srcset = (img.getAttribute && img.getAttribute('srcset')) || '';
                        if (src === '' && srcset === '') { fail(); return; }
                        if (img.complete && img.naturalWidth > 0) { resolve(); return; }
                        var url = img.currentSrc || src;
                        if (!url) { fail(); return; }
                        var probe = new Image();
                        probe.onload = function () { resolve(); };
                        probe.onerror = fail;
                        probe.src = url;
                    });
                });
            }
        });
    })();
    /* Text tracks: addTextTrack() was a no-op and textTracks returned a fresh
     * empty array. Provide a working TextTrack / TextTrackList / TextTrackCue
     * model: addTextTrack, a media element's live textTracks list with an async
     * 'addtrack' TrackEvent, a <track> element's .track and readyState, mode
     * validation, cue add/remove, and WebVTT loading of a <track src> (fetch +
     * a minimal cue parser, firing load/error). Cue timing/rendering is not
     * implemented. */
    (function () {
        if (typeof document === 'undefined') return;

        function eventTarget(obj) {
            var listeners = {};
            obj.addEventListener = function (type, cb) {
                if (!cb) return;
                (listeners[type] || (listeners[type] = [])).push(cb);
            };
            obj.removeEventListener = function (type, cb) {
                var a = listeners[type];
                if (a) { var i = a.indexOf(cb); if (i >= 0) a.splice(i, 1); }
            };
            obj.dispatchEvent = function (ev) {
                try {
                    if (ev && ev.target == null)
                        Object.defineProperty(ev, 'target',
                            { value: obj, configurable: true });
                } catch (e) {}
                var prev;
                try { prev = global.event; global.event = ev; } catch (e) {}
                var on = obj['on' + ev.type];
                if (typeof on === 'function') { try { on.call(obj, ev); } catch (e) {} }
                var a = listeners[ev.type];
                if (a) a.slice().forEach(function (cb) {
                    try { cb.call(obj, ev); } catch (e) {}
                });
                try { global.event = prev; } catch (e) {}
                return true;
            };
            return obj;
        }

        function adoptInterface(obj, name, arrayBase) {
            var iface = global[name];
            if (typeof iface !== 'function' || !iface.prototype) return obj;
            try {
                if (arrayBase &&
                    Object.getPrototypeOf(iface.prototype) !== Array.prototype)
                    Object.setPrototypeOf(iface.prototype, Array.prototype);
                Object.setPrototypeOf(obj, iface.prototype);
            } catch (e) {}
            return obj;
        }

        function makeCueList() {
            var list = [];
            list.getCueById = function (id) {
                for (var i = 0; i < this.length; i++)
                    if (this[i].id === id) return this[i];
                return null;
            };
            return adoptInterface(list, 'TextTrackCueList', true);
        }

        var MODES = { disabled: 1, hidden: 1, showing: 1 };

        function TextTrack(el, kind, label, language, id, mode) {
            var self = eventTarget({});
            var _mode = MODES[mode] ? mode : 'disabled';
            var cues = makeCueList();
            var active = makeCueList();
            self.oncuechange = null;
            self.__el = el || null;
            self.__cues = cues;
            Object.defineProperties(self, {
                kind:     { enumerable: true, value: kind || '' },
                label:    { enumerable: true, value: label || '' },
                language: { enumerable: true, value: language || '' },
                id:       { enumerable: true, value: id || '' },
                inBandMetadataTrackDispatchType: { enumerable: true, value: '' },
                mode: {
                    enumerable: true,
                    get: function () { return _mode; },
                    set: function (v) {
                        var s = (typeof v === 'string') ? v : String(v);
                        if (MODES[s]) {
                            _mode = s;
                            if (s !== 'disabled') {
                                if (typeof queueMicrotask === 'function')
                                    queueMicrotask(function () { maybeLoad(self); });
                                else maybeLoad(self);
                            }
                        }
                    }
                },
                cues:       { enumerable: true, get: function () {
                    return _mode === 'disabled' ? null : cues; } },
                activeCues: { enumerable: true, get: function () {
                    return _mode === 'disabled' ? null : active; } },
                addCue: { value: function (cue) {
                    if (!cue) return;
                    try { cue.track = self; } catch (e) {}
                    if (cues.indexOf(cue) >= 0) return;
                    var st = +cue.startTime, et = +cue.endTime;
                    var i = 0;
                    while (i < cues.length) {
                        var cst = +cues[i].startTime, cet = +cues[i].endTime;
                        if (cst > st || (cst === st && cet < et)) break;
                        i++;
                    }
                    cues.splice(i, 0, cue);
                } },
                removeCue: { value: function (cue) {
                    var i = cues.indexOf(cue);
                    if (i < 0) throw new DOMException('Cue not found', 'NotFoundError');
                    cues.splice(i, 1);
                    try { cue.track = null; } catch (e) {}
                } }
            });
            try {
                Object.defineProperty(self, Symbol.toStringTag,
                    { value: 'TextTrack', configurable: true });
            } catch (e) {}
            return adoptInterface(self, 'TextTrack', false);
        }

        function parseTimestamp(s) {
            var m = /^(?:(\d+):)?(\d{2}):(\d{2})\.(\d{3})$/.exec(s.trim());
            if (!m) return NaN;
            return (m[1] ? +m[1] * 3600 : 0) + (+m[2]) * 60 + (+m[3]) + (+m[4]) / 1000;
        }

        function parseVtt(text, track) {
            var lines = String(text).replace(/\r\n|\r/g, '\n').split('\n');
            if (!/^﻿?WEBVTT/.test(lines[0] || '')) return false;
            var i = 1;
            while (i < lines.length) {
                while (i < lines.length && lines[i].trim() === '') i++;
                if (i >= lines.length) break;
                var id = '';
                if (lines[i].indexOf('-->') < 0) { id = lines[i]; i++; }
                if (i >= lines.length || lines[i].indexOf('-->') < 0) {
                    while (i < lines.length && lines[i].trim() !== '') i++;
                    continue;
                }
                var tm = lines[i].split('-->');
                var start = parseTimestamp(tm[0]);
                var end = parseTimestamp((tm[1] || '').trim().split(/\s+/)[0] || '');
                i++;
                var textLines = [];
                while (i < lines.length && lines[i].trim() !== '') {
                    textLines.push(lines[i]); i++;
                }
                if (!isNaN(start) && !isNaN(end) && typeof global.VTTCue === 'function') {
                    try {
                        var cue = new global.VTTCue(start, end, textLines.join('\n'));
                        cue.id = id;
                        track.addCue(cue);
                    } catch (e) {}
                }
            }
            return true;
        }

        var RS = { NONE: 0, LOADING: 1, LOADED: 2, ERROR: 3 };

        function maybeLoad(track) {
            var el = track.__el;
            if (!el || track.__loading) return;
            var parent = el.parentNode;
            if (!parent || !isMedia(parent)) return;
            var src = el.getAttribute ? (el.getAttribute('src') || '') : '';
            if (track.__loadedSrc === src && (src || track.__triedEmpty)) return;
            if (!src) {
                track.__triedEmpty = true;
                track.__loadedSrc = src;
                el.__trackRS = RS.ERROR;
                var fail = function () {
                    try { el.dispatchEvent(new Event('error')); } catch (e) {}
                };
                if (typeof queueMicrotask === 'function') queueMicrotask(fail);
                else setTimeout(fail, 0);
                return;
            }
            track.__loading = true;
            track.__loadedSrc = src;
            el.__trackRS = RS.LOADING;
            var resolved = src;
            try { resolved = new URL(src, document.baseURI).href; } catch (e) {}
            (typeof fetch === 'function'
                ? fetch(resolved).then(function (r) {
                    if (!r.ok) throw new Error('http ' + r.status);
                    return r.text();
                  })
                : Promise.reject(new Error('no fetch'))
            ).then(function (text) {
                track.__loading = false;
                var ok = parseVtt(text, track);
                el.__trackRS = ok ? RS.LOADED : RS.ERROR;
                try { el.dispatchEvent(new Event(ok ? 'load' : 'error')); } catch (e) {}
            }).catch(function () {
                track.__loading = false;
                el.__trackRS = RS.ERROR;
                try { el.dispatchEvent(new Event('error')); } catch (e) {}
            });
        }

        function trackListFor(el) {
            if (el.__ndTT) return el.__ndTT;
            var list = eventTarget([]);
            list.onaddtrack = null;
            list.onremovetrack = null;
            list.onchange = null;
            list.getTrackById = function (id) {
                for (var i = 0; i < this.length; i++)
                    if (this[i].id === id) return this[i];
                return null;
            };
            try {
                Object.defineProperty(list, Symbol.toStringTag,
                    { value: 'TextTrackList', configurable: true });
            } catch (e) {}
            adoptInterface(list, 'TextTrackList', true);
            try {
                Object.defineProperty(el, '__ndTT',
                    { value: list, configurable: true });
            } catch (e) { el.__ndTT = list; }
            return list;
        }

        function addTrack(list, track) {
            if (list.indexOf(track) >= 0) return;
            list.push(track);
            var fire = function () {
                var ev;
                try { ev = new TrackEvent('addtrack'); }
                catch (e) { ev = { type: 'addtrack' }; }
                try { ev.track = track; } catch (e) {}
                list.dispatchEvent(ev);
            };
            if (typeof queueMicrotask === 'function') queueMicrotask(fire);
            else setTimeout(fire, 0);
        }

        var proto = Object.getPrototypeOf(document.createElement('video'));
        var trackProto = typeof global.HTMLTrackElement === 'function'
            ? global.HTMLTrackElement.prototype : proto;

        function isMedia(el) {
            var nm = el && el.tagName ? el.tagName.toLowerCase() : '';
            return nm === 'video' || nm === 'audio';
        }
        function isTrack(el) {
            return el && el.tagName && el.tagName.toLowerCase() === 'track';
        }

        Object.defineProperty(proto, 'textTracks', {
            configurable: true, enumerable: true,
            get: function () {
                var list = trackListFor(this);
                if (isMedia(this) && this.children) {
                    for (var i = 0; i < this.children.length; i++)
                        if (isTrack(this.children[i]))
                            considerTrack(this.children[i]);
                }
                return list;
            }
        });

        Object.defineProperty(proto, 'addTextTrack', {
            configurable: true, writable: true, enumerable: true,
            value: function (kind, label, language) {
                var k = (kind == null) ? '' : String(kind);
                var valid = { subtitles: 1, captions: 1, descriptions: 1,
                              chapters: 1, metadata: 1 };
                if (!valid[k])
                    throw new TypeError("Failed to execute 'addTextTrack': " +
                        "The provided value '" + k + "' is not a valid 'TextTrackKind'.");
                var track = TextTrack(null, k, label, language, '', 'hidden');
                addTrack(trackListFor(this), track);
                return track;
            }
        });

        Object.defineProperty(trackProto, 'track', {
            configurable: true, enumerable: true,
            get: function () {
                if (!isTrack(this)) return null;
                if (!this.__ndTrack) {
                    var kind = (this.getAttribute('kind') || 'subtitles').toLowerCase();
                    var valid = { subtitles: 1, captions: 1, descriptions: 1,
                                  chapters: 1, metadata: 1 };
                    if (!valid[kind]) kind = 'metadata';
                    var t = TextTrack(this, kind, this.getAttribute('label') || '',
                                      this.getAttribute('srclang') || '',
                                      this.id || '', 'disabled');
                    try { Object.defineProperty(this, '__ndTrack',
                        { value: t, configurable: true }); }
                    catch (e) { this.__ndTrack = t; }
                }
                var parent = this.parentNode;
                if (parent && isMedia(parent)) {
                    addTrack(trackListFor(parent), this.__ndTrack);
                    var self = this;
                    if (typeof queueMicrotask === 'function')
                        queueMicrotask(function () { maybeLoad(self.__ndTrack); });
                }
                return this.__ndTrack;
            }
        });

        Object.defineProperty(trackProto, 'readyState', {
            configurable: true, enumerable: true,
            get: function () { return this.__trackRS || 0; }
        });

        if (typeof global.HTMLTrackElement === 'function') {
            ['NONE', 'LOADING', 'LOADED', 'ERROR'].forEach(function (k) {
                try {
                    Object.defineProperty(global.HTMLTrackElement, k,
                        { value: RS[k], enumerable: true });
                    Object.defineProperty(global.HTMLTrackElement.prototype, k,
                        { value: RS[k], enumerable: true, configurable: true });
                } catch (e) {}
            });
        }

        if (typeof global.VTTCue === 'function' && global.VTTCue.prototype) {
            try {
                var vp = global.VTTCue.prototype;
                if (!Object.getOwnPropertyDescriptor(vp, 'id')) {
                    Object.defineProperty(vp, 'id', {
                        configurable: true, enumerable: true,
                        get: function () {
                            return this.__nd_id === undefined ? '' : this.__nd_id;
                        },
                        set: function (v) { this.__nd_id = String(v); }
                    });
                }
            } catch (e) {}
        }

        /* The spec's track processing model runs on connection, not on JS
         * access, so a <track> appended to a media element (without anyone
         * touching .track) must still load. A document-wide observer would tax
         * every page, so it is started lazily only once a track/audio/video
         * element is created. */
        var observing = false;
        function considerTrack(el) {
            if (!isTrack(el)) return;
            var parent = el.parentNode;
            if (!parent || !isMedia(parent)) return;
            var track = el.track;
            var isDefault = el.default === true ||
                (el.hasAttribute && el.hasAttribute('default'));
            if (track.mode === 'disabled' && isDefault)
                track.mode = 'hidden';
            if (track.mode !== 'disabled') {
                if (typeof queueMicrotask === 'function')
                    queueMicrotask(function () { maybeLoad(track); });
                else maybeLoad(track);
            }
        }
        function scanForTracks(node) {
            if (!node || node.nodeType !== 1) return;
            considerTrack(node);
            if (node.querySelectorAll) {
                var ts = node.querySelectorAll('track');
                for (var i = 0; i < ts.length; i++) considerTrack(ts[i]);
            }
        }
        function removeTrackNode(parent, node) {
            if (!isTrack(node) || !node.__ndTrack || !parent || !parent.__ndTT)
                return;
            var list = parent.__ndTT;
            var idx = list.indexOf(node.__ndTrack);
            if (idx < 0) return;
            var track = node.__ndTrack;
            list.splice(idx, 1);
            var fire = function () {
                var ev;
                try { ev = new TrackEvent('removetrack'); }
                catch (e) { ev = { type: 'removetrack' }; }
                try { ev.track = track; } catch (e) {}
                list.dispatchEvent(ev);
            };
            if (typeof queueMicrotask === 'function') queueMicrotask(fire);
            else setTimeout(fire, 0);
        }
        function onMutations(muts) {
            for (var i = 0; i < muts.length; i++) {
                var m = muts[i];
                for (var j = 0; j < m.addedNodes.length; j++)
                    scanForTracks(m.addedNodes[j]);
                for (var k = 0; k < m.removedNodes.length; k++)
                    removeTrackNode(m.target, m.removedNodes[k]);
            }
        }
        function ensureObserver() {
            if (observing || typeof MutationObserver !== 'function' ||
                !document.documentElement) return;
            observing = true;
            new MutationObserver(onMutations).observe(document.documentElement,
                       { childList: true, subtree: true });
            scanForTracks(document.documentElement);
        }
        function observeMedia(el) {
            if (typeof MutationObserver !== 'function') return;
            try {
                new MutationObserver(onMutations).observe(el,
                    { childList: true, subtree: true });
            } catch (e) {}
        }
        if (typeof document.createElement === 'function') {
            var origCreate = document.createElement;
            var wrapCreate = function createElement(name) {
                var el = origCreate.apply(this, arguments);
                var n = ('' + name).toLowerCase();
                if (n === 'track' || n === 'video' || n === 'audio')
                    ensureObserver();
                if (n === 'video' || n === 'audio')
                    observeMedia(el);
                return el;
            };
            try {
                Object.defineProperty(wrapCreate, 'length',
                    { value: 1, configurable: true });
            } catch (e) {}
            nativeize(wrapCreate, 'createElement');
            document.createElement = wrapCreate;
        }
    })();

    (function processingInstructionAttributes() {
        var PI = global.ProcessingInstruction;
        if (typeof PI !== 'function' || !PI.prototype) return;
        var proto = PI.prototype;

        function domEx(name, msg) {
            try { return new global.DOMException(msg || name, name); }
            catch (e) {
                var er = new Error(msg || name);
                er.name = name;
                return er;
            }
        }
        function isWs(ch) {
            return ch === ' ' || ch === '\t' || ch === '\n' ||
                   ch === '\r' || ch === '\f';
        }
        function validName(name) {
            if (typeof name !== 'string' || !name.length) return false;
            for (var i = 0; i < name.length; i++) {
                var ch = name[i];
                if (isWs(ch) || ch === '=' || ch === '>' || ch === '/' ||
                    ch === '"' || ch === "'")
                    return false;
            }
            return true;
        }
        function unescapeValue(v) {
            if (v.indexOf('&') < 0) return v;
            return v.replace(/&quot;/g, '"').replace(/&nbsp;/g, '\u00a0')
                    .replace(/&lt;/g, '<').replace(/&gt;/g, '>')
                    .replace(/&amp;/g, '&');
        }
        function escapeValue(v) {
            return String(v).replace(/&/g, '&amp;').replace(/"/g, '&quot;')
                            .replace(/</g, '&lt;').replace(/>/g, '&gt;')
                            .replace(/\u00a0/g, '&nbsp;');
        }
        function parseAttrs(data) {
            var s = data === null || data === undefined ? '' : String(data);
            var attrs = [];
            var i = 0, n = s.length;
            while (i < n) {
                while (i < n && isWs(s[i])) i++;
                if (i >= n) break;
                var nameStart = i;
                while (i < n && s[i] !== '=' && !isWs(s[i])) i++;
                var name = s.slice(nameStart, i);
                if (!validName(name) || i >= n || s[i] !== '=') return null;
                i++;
                if (i >= n || s[i] !== '"') return null;
                i++;
                var vs = i;
                while (i < n && s[i] !== '"') i++;
                if (i >= n) return null;
                attrs.push([name, unescapeValue(s.slice(vs, i))]);
                i++;
                if (i < n && !isWs(s[i])) return null;
            }
            return attrs;
        }
        function serializeAttrs(attrs) {
            var parts = [];
            for (var i = 0; i < attrs.length; i++)
                parts.push(attrs[i][0] + '="' +
                           escapeValue(attrs[i][1]) + '"');
            return parts.join(' ');
        }
        function findAttr(attrs, name) {
            for (var i = 0; i < attrs.length; i++)
                if (attrs[i][0] === name) return i;
            return -1;
        }
        function def(name, fn) {
            Object.defineProperty(proto, name, {
                configurable: true, writable: true, value: fn
            });
        }
        if (!Object.getOwnPropertyDescriptor(proto, 'target')) {
            Object.defineProperty(proto, 'target', {
                configurable: true,
                get: function () { return this.nodeName; }
            });
        }
        def('hasAttributes', function () {
            var a = parseAttrs(this.data);
            return !!a && a.length > 0;
        });
        def('getAttributeNames', function () {
            var a = parseAttrs(this.data);
            if (!a) return [];
            var out = [];
            for (var i = 0; i < a.length; i++) out.push(a[i][0]);
            return out;
        });
        def('getAttribute', function (name) {
            var a = parseAttrs(this.data);
            if (!a) return null;
            var i = findAttr(a, String(name));
            return i < 0 ? null : a[i][1];
        });
        def('hasAttribute', function (name) {
            var a = parseAttrs(this.data);
            return !!a && findAttr(a, String(name)) >= 0;
        });
        def('setAttribute', function (name, value) {
            name = String(name);
            if (!validName(name))
                throw domEx('InvalidCharacterError',
                            'invalid attribute name');
            var a = parseAttrs(this.data) || [];
            var i = findAttr(a, name);
            if (i < 0) a.push([name, String(value)]);
            else a[i] = [name, String(value)];
            this.data = serializeAttrs(a);
        });
        def('removeAttribute', function (name) {
            var a = parseAttrs(this.data);
            if (!a) return;
            var i = findAttr(a, String(name));
            if (i < 0) return;
            a.splice(i, 1);
            this.data = serializeAttrs(a);
        });
        def('toggleAttribute', function (name, force) {
            name = String(name);
            if (!validName(name))
                throw domEx('InvalidCharacterError',
                            'invalid attribute name');
            var a = parseAttrs(this.data) || [];
            var i = findAttr(a, name);
            if (i >= 0) {
                if (force === true) return true;
                a.splice(i, 1);
                this.data = serializeAttrs(a);
                return false;
            }
            if (force === false) return false;
            a.push([name, '']);
            this.data = serializeAttrs(a);
            return true;
        });
    })();

    (function () {
        var nav = global.navigator;
        if (!nav || typeof global.Navigator !== 'function') return;
        var Np = global.Navigator.prototype;
        if (!Np || Object.getPrototypeOf(nav) !== Np) return;
        Object.getOwnPropertyNames(nav).forEach(function (k) {
            var d = Object.getOwnPropertyDescriptor(nav, k);
            if (!d || !d.configurable) return;
            try {
                delete nav[k];
                if (!Object.prototype.hasOwnProperty.call(Np, k))
                    Object.defineProperty(Np, k, d);
            } catch (e) {}
        });
    })();

    (function () {
        if (typeof global.__ns_anim_list !== 'function') return;
        var registry = new WeakMap();
        var timelineStart = (typeof performance !== 'undefined' && performance.now) ? 0 : 0;
        function now() {
            return (typeof performance !== 'undefined' && performance.now)
                ? performance.now() : Date.now();
        }
        function DocumentTimeline() {}
        Object.defineProperty(DocumentTimeline.prototype, 'currentTime', {
            get: function () { return now() - timelineStart; },
            configurable: true
        });
        if (!global.DocumentTimeline) global.DocumentTimeline = DocumentTimeline;
        if (typeof document !== 'undefined' && !('timeline' in document)) {
            var tl = new DocumentTimeline();
            Object.defineProperty(document, 'timeline', {
                get: function () { return tl; }, configurable: true
            });
        }

        function listeners(obj) {
            if (!obj.__listeners) obj.__listeners = Object.create(null);
            return obj.__listeners;
        }

        function AnimationEffect(anim) {
            Object.defineProperty(this, '__anim', { value: anim, writable: true });
        }
        AnimationEffect.prototype.getTiming = function () {
            var q = (this.__anim && this.__anim.__query()) || {};
            var isTransition = this.__anim && this.__anim.__kind === 'transition';
            var o = this.__options || {};
            if (typeof o === 'number') o = { duration: o };
            return {
                delay: q.delayMs !== undefined ? q.delayMs : (o.delay || 0),
                endDelay: o.endDelay || 0,
                fill: isTransition ? 'backwards' : (q.fill || o.fill || 'none'),
                iterationStart: o.iterationStart || 0,
                iterations: q.iterations !== undefined ? q.iterations
                            : (o.iterations === undefined ? 1 : o.iterations),
                duration: q.durationMs !== undefined ? q.durationMs
                          : (typeof o.duration === 'number' ? o.duration : 0),
                direction: isTransition ? 'normal' : (q.direction || o.direction || 'normal'),
                easing: o.easing || 'linear'
            };
        };
        AnimationEffect.prototype.getComputedTiming = function () {
            var t = this.getTiming();
            var q = this.__anim ? this.__anim.__query() : null;
            var iterations = t.iterations;
            var active = t.duration * iterations;
            if (t.duration === 0 || iterations === 0) active = 0;
            var local = q ? q.currentMs : null;
            var progress = null, iter = null;
            if (local !== null) {
                var el = local - t.delay;
                var fillsBack = t.fill === 'backwards' || t.fill === 'both';
                var fillsFwd = t.fill === 'forwards' || t.fill === 'both';
                var overall = null;
                if (el < 0) {
                    if (fillsBack) overall = 0;
                } else if (el >= active) {
                    if (fillsFwd || (q && q.active)) overall = isFinite(active) ? active : el;
                } else {
                    overall = el;
                }
                if (overall !== null) {
                    if (t.duration > 0) {
                        var raw = overall / t.duration;
                        iter = Math.floor(raw);
                        var simple = raw - iter;
                        var atEnd = isFinite(iterations) && overall >= active && active > 0;
                        if (atEnd) {
                            iter = Math.max(Math.ceil(iterations) - 1, 0);
                            simple = iterations - iter;
                            if (simple > 1) simple = 1;
                        }
                        if (simple === 0 && iter > 0 && overall >= active && !atEnd) {
                            iter -= 1; simple = 1;
                        }
                        progress = simple;
                    } else {
                        iter = isFinite(iterations) ? Math.max(Math.ceil(iterations) - 1, 0) : Infinity;
                        if (el < 0) { progress = 0; iter = 0; }
                        else progress = isFinite(iterations) ? Math.min(iterations - iter, 1) : 1;
                    }
                    var reversed = t.direction === 'reverse' ||
                        (t.direction === 'alternate' && (iter % 2) === 1) ||
                        (t.direction === 'alternate-reverse' && (iter % 2) === 0);
                    if (reversed) progress = 1 - progress;
                }
            }
            return Object.assign(t, {
                activeDuration: active,
                endTime: Math.max(t.delay + active + t.endDelay, 0),
                localTime: local,
                progress: progress,
                currentIteration: iter
            });
        };
        AnimationEffect.prototype.updateTiming = function () {};
        Object.defineProperty(AnimationEffect.prototype, 'target', {
            get: function () { return this.__anim.__el; }, configurable: true
        });
        Object.defineProperty(AnimationEffect.prototype, 'pseudoElement', {
            get: function () { return null; }, configurable: true
        });
        function kebab(name) {
            if (name === 'cssFloat') return 'float';
            if (name === 'cssOffset') return 'offset';
            return name.replace(/[A-Z]/g, function (c) { return '-' + c.toLowerCase(); });
        }
        function normalizeKeyframes(input) {
            var frames = [];
            if (input === null || input === undefined) return frames;
            var isArrayLike = Array.isArray(input) ||
                (typeof input === 'object' && typeof input[Symbol.iterator] === 'function');
            if (isArrayLike) {
                var list = Array.from(input);
                for (var i = 0; i < list.length; i++) {
                    var kf = list[i];
                    if (kf === null || typeof kf !== 'object') throw new TypeError('Keyframe must be an object');
                    var frame = { offset: null, easing: 'linear', composite: 'auto', __props: {} };
                    for (var k in kf) {
                        if (!Object.prototype.hasOwnProperty.call(kf, k)) continue;
                        if (k === 'offset') {
                            if (kf.offset !== null && kf.offset !== undefined) {
                                var o = Number(kf.offset);
                                if (!(o >= 0 && o <= 1)) throw new TypeError('Keyframe offset out of range');
                                frame.offset = o;
                            }
                        } else if (k === 'easing') frame.easing = String(kf.easing);
                        else if (k === 'composite') frame.composite = String(kf.composite);
                        else frame.__props[kebab(k)] = String(kf[k]);
                    }
                    frames.push(frame);
                }
            } else if (typeof input === 'object') {
                var propMax = 0, names = [];
                for (var p in input) {
                    if (!Object.prototype.hasOwnProperty.call(input, p) ||
                        p === 'offset' || p === 'easing' || p === 'composite') continue;
                    names.push(p);
                    var v = input[p];
                    propMax = Math.max(propMax, Array.isArray(v) ? v.length : 1);
                }
                if (propMax < 2) propMax = 2;
                var byOffset = {};
                names.forEach(function (p) {
                    var v = input[p];
                    var vals = Array.isArray(v) ? v : [v];
                    if (vals.length === 1) {
                        byOffset[1] = byOffset[1] || {};
                        byOffset[1][kebab(p)] = String(vals[0]);
                        return;
                    }
                    for (var i = 0; i < vals.length; i++) {
                        var off = vals.length === 1 ? 1 : i / (vals.length - 1);
                        byOffset[off] = byOffset[off] || {};
                        byOffset[off][kebab(p)] = String(vals[i]);
                    }
                });
                Object.keys(byOffset).map(Number).sort(function (a, b) { return a - b; })
                    .forEach(function (off) {
                        frames.push({ offset: off, easing: typeof input.easing === 'string' ? input.easing : 'linear',
                                      composite: 'auto', __props: byOffset[off] });
                    });
            }
            var last = -1;
            for (var j = 0; j < frames.length; j++) {
                if (frames[j].offset !== null) {
                    if (frames[j].offset < last) throw new TypeError('Keyframe offsets must be monotonically increasing');
                    last = frames[j].offset;
                }
            }
            if (frames.length > 0) {
                if (frames[0].offset === null) frames[0].offset = 0;
                if (frames[frames.length - 1].offset === null) frames[frames.length - 1].offset = 1;
                var i0 = 0;
                while (i0 < frames.length) {
                    if (frames[i0].offset !== null) { i0++; continue; }
                    var i1 = i0;
                    while (frames[i1].offset === null) i1++;
                    var a = frames[i0 - 1].offset, b = frames[i1].offset, n = i1 - i0 + 1;
                    for (var m = i0; m < i1; m++) frames[m].offset = a + (b - a) * (m - i0 + 1) / n;
                    i0 = i1;
                }
            }
            return frames;
        }
        function frameToCss(frame) {
            var out = [];
            for (var p in frame.__props) out.push(p + ': ' + frame.__props[p]);
            return out.join('; ');
        }
        function KeyframeEffect(target, keyframes, options) {
            AnimationEffect.call(this, null);
            if (target instanceof KeyframeEffect) {
                this.__target = target.__target;
                this.__frames = target.__frames.slice();
                this.__options = Object.assign({}, target.__options);
                return;
            }
            this.__target = target || null;
            this.__frames = normalizeKeyframes(keyframes);
            this.__options = typeof options === 'number' ? { duration: options } : (options || {});
        }
        KeyframeEffect.prototype = Object.create(AnimationEffect.prototype);
        KeyframeEffect.prototype.constructor = KeyframeEffect;
        function cssKeyframes(anim) {
            var raw = global.__ns_anim_keyframes(anim.__el, anim.__prop) || [];
            var merged = [];
            raw.forEach(function (f) {
                if (f.count === 0) return;
                var last = merged[merged.length - 1];
                var target = null;
                for (var i = merged.length - 1; i >= 0; i--) {
                    if (merged[i].offset === f.offset && merged[i].easing === f.easing &&
                        merged[i].composite === f.composite) { target = merged[i]; break; }
                    if (merged[i].offset !== f.offset) break;
                }
                if (!target) {
                    target = { offset: f.offset, computedOffset: f.offset, easing: f.easing,
                               composite: f.composite, __props: {} };
                    merged.push(target);
                }
                for (var p in f.props) target.__props[p] = f.props[p];
            });
            merged.sort(function (a, b) { return a.offset - b.offset; });
            var allProps = {};
            merged.forEach(function (m) { for (var p in m.__props) allProps[p] = true; });
            var runEasing = raw.length && raw[0].runEasing ? raw[0].runEasing : 'ease';
            var cs = null;
            [0, 1].forEach(function (end) {
                var missing = [];
                for (var p in allProps) {
                    var has = merged.some(function (m) { return m.offset === end && p in m.__props; });
                    if (!has) missing.push(p);
                }
                if (!missing.length) return;
                if (!cs) cs = getComputedStyle(anim.__el);
                var existing = null;
                for (var i = merged.length - 1; i >= 0; i--)
                    if (merged[i].offset === end) { existing = merged[i]; break; }
                var target = existing || { offset: end, computedOffset: end, easing: runEasing,
                                           composite: 'replace', __props: {} };
                missing.forEach(function (p) {
                    var base = typeof global.__ns_anim_base_value === 'function'
                        ? global.__ns_anim_base_value(anim.__el, p) : null;
                    target.__props[p] = base !== null && base !== undefined ? base : cs.getPropertyValue(p);
                });
                if (!existing) {
                    if (end === 0) merged.unshift(target); else merged.push(target);
                }
            });
            return merged.map(function (m) {
                var o = { offset: m.offset, computedOffset: m.computedOffset,
                          easing: m.easing, composite: m.composite };
                for (var p in m.__props) {
                    var camel = p.replace(/-([a-z])/g, function (x, c) { return c.toUpperCase(); });
                    o[p === 'float' ? 'cssFloat' : camel] = m.__props[p];
                }
                return o;
            });
        }
        AnimationEffect.prototype.getKeyframes = function () {
            if (this.__anim && this.__anim.__kind === 'animation' &&
                typeof global.__ns_anim_keyframes === 'function')
                return cssKeyframes(this.__anim);
            return [];
        };
        KeyframeEffect.prototype.getKeyframes = function () {
            if (this.__anim && this.__anim.__kind === 'animation' &&
                typeof global.__ns_anim_keyframes === 'function')
                return cssKeyframes(this.__anim);
            return this.__frames.map(function (f) {
                var o = { offset: f.offset, computedOffset: f.offset, easing: f.easing, composite: f.composite };
                for (var p in f.__props) {
                    var camel = p.replace(/-([a-z])/g, function (m, c) { return c.toUpperCase(); });
                    o[p === 'float' ? 'cssFloat' : camel] = f.__props[p];
                }
                return o;
            });
        };
        KeyframeEffect.prototype.setKeyframes = function (k) { this.__frames = normalizeKeyframes(k); };
        Object.defineProperty(KeyframeEffect.prototype, 'target', {
            get: function () { return this.__target; },
            set: function (v) { this.__target = v; },
            configurable: true
        });
        Object.defineProperty(KeyframeEffect.prototype, 'composite', {
            get: function () { return 'replace'; }, set: function () {}, configurable: true
        });
        global.AnimationEffect = AnimationEffect;
        global.KeyframeEffect = KeyframeEffect;

        function Animation(effect, timeline) {
            if (effect !== undefined && effect !== null && !(effect instanceof AnimationEffect))
                throw new TypeError('Animation effect must be an AnimationEffect');
            this.__el = effect && effect.__target ? effect.__target : null;
            this.__prop = null;
            this.__kind = 'generic';
            this.__gen = 0;
            this.__name = null;
            this.__effectObj = effect || null;
            if (effect) effect.__anim = this;
            this.__timeline = timeline || (typeof document !== 'undefined' ? document.timeline : null);
            this.id = '';
            this.onfinish = null;
            this.oncancel = null;
            this.onremove = null;
            this.__playbackRate = 1;
        }
        Animation.prototype.__query = function () {
            if (!this.__el || this.__kind === 'generic') return null;
            var q = global.__ns_anim_query(this.__el, this.__prop);
            if (!q || q.generation !== this.__gen) return null;
            return q;
        };
        Object.defineProperty(Animation.prototype, 'effect', {
            get: function () {
                if (!this.__effectObj) this.__effectObj = new AnimationEffect(this);
                return this.__effectObj;
            },
            set: function (v) { this.__effectObj = v; },
            configurable: true
        });
        Object.defineProperty(Animation.prototype, 'timeline', {
            get: function () { return this.__timeline; },
            set: function (v) {
                var had = this.__timeline;
                this.__timeline = v;
                if (v === null && had && this.__el && this.__kind !== 'generic') {
                    this.__cancelling = true;
                    global.__ns_anim_control(this.__el, this.__prop, 'cancel');
                    this.__cancelling = false;
                    this.__settle(false);
                }
            },
            configurable: true
        });
        function timelineNow(anim) {
            return anim.__timeline ? anim.__timeline.currentTime : null;
        }
        Object.defineProperty(Animation.prototype, 'currentTime', {
            get: function () {
                var q = this.__query();
                if (q) return q.currentMs;
                if (this.__kind === 'generic') return 0;
                return this.__lastTime === undefined ? null : this.__lastTime;
            },
            set: function (v) {
                if (v === null) throw new TypeError('currentTime may not be set to null');
                if (!this.__el || this.__kind === 'generic') return;
                global.__ns_anim_seek(this.__el, this.__prop, Number(v));
            },
            configurable: true
        });
        Object.defineProperty(Animation.prototype, 'startTime', {
            get: function () {
                var q = this.__query();
                var now = timelineNow(this);
                if (!q || q.paused || q.pending || now === null) return null;
                return now - q.currentMs;
            },
            set: function (v) {
                if (!this.__el || this.__kind === 'generic') return;
                if (v === null) {
                    global.__ns_anim_control(this.__el, this.__prop, 'pause');
                    return;
                }
                var now = timelineNow(this);
                if (now === null) return;
                var q = this.__query();
                if (q && q.paused) global.__ns_anim_control(this.__el, this.__prop, 'play');
                global.__ns_anim_seek(this.__el, this.__prop, now - Number(v));
            },
            configurable: true
        });
        Object.defineProperty(Animation.prototype, 'playState', {
            get: function () {
                var q = this.__query();
                if (!q) return this.__kind === 'generic' ? 'finished' : 'idle';
                if (q.finished) return 'finished';
                if (q.paused) return 'paused';
                return 'running';
            },
            configurable: true
        });
        Object.defineProperty(Animation.prototype, 'pending', {
            get: function () { var q = this.__query(); return !!(q && q.pending); },
            configurable: true
        });
        Object.defineProperty(Animation.prototype, 'replaceState', {
            get: function () { return 'active'; }, configurable: true
        });
        Object.defineProperty(Animation.prototype, 'playbackRate', {
            get: function () { return this.__playbackRate; },
            set: function (v) { this.__playbackRate = Number(v); },
            configurable: true
        });
        Object.defineProperty(Animation.prototype, 'ready', {
            get: function () { return Promise.resolve(this); }, configurable: true
        });
        Object.defineProperty(Animation.prototype, 'finished', {
            get: function () {
                if (this.__finished) return this.__finished;
                var self = this;
                this.__finished = new Promise(function (resolve, reject) {
                    var q = self.__query();
                    if (!q || q.finished) { resolve(self); return; }
                    self.__resolveFinished = resolve;
                    self.__rejectFinished = reject;
                });
                return this.__finished;
            },
            configurable: true
        });
        Animation.prototype.__settle = function (finished) {
            var q = this.__query();
            if (q) this.__lastTime = q.currentMs;
            if (finished) {
                if (this.__resolveFinished) this.__resolveFinished(this);
                var ev = { type: 'finish', target: this, currentTime: q ? q.currentMs : null, timelineTime: timelineNow(this) };
                if (typeof this.onfinish === 'function') this.onfinish(ev);
                this.__dispatch('finish', ev);
            } else {
                if (this.__rejectFinished) {
                    var err = new DOMException('The user aborted a request.', 'AbortError');
                    this.__rejectFinished(err);
                }
                this.__finished = null;
                var cev = { type: 'cancel', target: this, currentTime: null, timelineTime: timelineNow(this) };
                if (typeof this.oncancel === 'function') this.oncancel(cev);
                this.__dispatch('cancel', cev);
            }
            this.__resolveFinished = null;
            this.__rejectFinished = null;
        };
        Animation.prototype.__dispatch = function (type, ev) {
            var ls = listeners(this)[type];
            if (!ls) return;
            ls.slice().forEach(function (fn) { try { fn.call(this, ev); } catch (e) {} }, this);
        };
        Animation.prototype.addEventListener = function (type, fn) {
            if (typeof fn !== 'function') return;
            var ls = listeners(this);
            (ls[type] = ls[type] || []).push(fn);
        };
        Animation.prototype.removeEventListener = function (type, fn) {
            var ls = listeners(this)[type];
            if (!ls) return;
            var i = ls.indexOf(fn);
            if (i >= 0) ls.splice(i, 1);
        };
        Animation.prototype.dispatchEvent = function (ev) { this.__dispatch(ev.type, ev); return true; };
        Animation.prototype.__startScript = function () {
            var effect = this.__effectObj;
            if (!effect || !(effect instanceof KeyframeEffect) || !effect.__target ||
                typeof global.__ns_anim_animate !== 'function') return false;
            var stops = effect.__frames.map(function (f) { return { offset: f.offset, css: frameToCss(f) }; });
            var o = effect.__options || {};
            var timing = {
                duration: typeof o.duration === 'number' ? o.duration : 0,
                delay: o.delay || 0,
                iterations: o.iterations === undefined ? 1 : o.iterations,
                direction: o.direction || 'normal',
                fill: (o.fill && o.fill !== 'auto') ? o.fill : 'none',
                easing: o.easing || 'linear'
            };
            var r = global.__ns_anim_animate(effect.__target, stops, timing);
            if (!r) return false;
            this.__el = effect.__target;
            this.__prop = '@' + r.run;
            this.__gen = r.generation;
            this.__kind = 'script';
            this.__finished = null;
            var map = registry.get(this.__el);
            if (!map) { map = Object.create(null); registry.set(this.__el, map); }
            map['@' + r.run + ':' + r.generation] = this;
            return true;
        };
        Animation.prototype.play = function () {
            if (this.__kind === 'generic') { this.__startScript(); return; }
            if (this.__el) {
                global.__ns_anim_control(this.__el, this.__prop, 'play');
                this.__finished = null;
            }
        };
        Animation.prototype.pause = function () {
            if (this.__el && this.__kind !== 'generic') global.__ns_anim_control(this.__el, this.__prop, 'pause');
        };
        Animation.prototype.finish = function () {
            if (this.__el && this.__kind !== 'generic') global.__ns_anim_control(this.__el, this.__prop, 'finish');
        };
        Animation.prototype.cancel = function () {
            if (this.__el && this.__kind !== 'generic') {
                this.__cancelling = true;
                global.__ns_anim_control(this.__el, this.__prop, 'cancel');
                this.__cancelling = false;
                this.__settle(false);
            }
        };
        Animation.prototype.reverse = function () {};
        Animation.prototype.updatePlaybackRate = function (r) { this.__playbackRate = Number(r); };
        Animation.prototype.commitStyles = function () {};
        Animation.prototype.persist = function () {};
        global.Animation = Animation;
        function tag(ctor, name) {
            if (typeof Symbol !== 'undefined' && Symbol.toStringTag)
                Object.defineProperty(ctor.prototype, Symbol.toStringTag, { value: name, configurable: true });
        }
        tag(Animation, 'Animation');
        tag(AnimationEffect, 'AnimationEffect');
        tag(KeyframeEffect, 'KeyframeEffect');
        tag(DocumentTimeline, 'DocumentTimeline');

        function CSSTransition() { Animation.apply(this, arguments); }
        CSSTransition.prototype = Object.create(Animation.prototype);
        CSSTransition.prototype.constructor = CSSTransition;
        Object.defineProperty(CSSTransition.prototype, 'transitionProperty', {
            get: function () { return this.__prop; }, configurable: true
        });
        tag(CSSTransition, 'CSSTransition');
        global.CSSTransition = CSSTransition;

        function CSSAnimation() { Animation.apply(this, arguments); }
        CSSAnimation.prototype = Object.create(Animation.prototype);
        CSSAnimation.prototype.constructor = CSSAnimation;
        Object.defineProperty(CSSAnimation.prototype, 'animationName', {
            get: function () { return this.__name; }, configurable: true
        });
        tag(CSSAnimation, 'CSSAnimation');
        global.CSSAnimation = CSSAnimation;

        function attachEndListeners(anim) {
            var el = anim.__el;
            var isTransition = anim.__kind === 'transition';
            var endType = isTransition ? 'transitionend' : 'animationend';
            var cancelType = isTransition ? 'transitioncancel' : 'animationcancel';
            function matches(e) {
                return isTransition ? e.propertyName === anim.__prop
                                    : e.animationName === anim.__name;
            }
            function onEnd(e) {
                if (!matches(e)) return;
                el.removeEventListener(endType, onEnd);
                el.removeEventListener(cancelType, onCancel);
                anim.__settle(true);
            }
            function onCancel(e) {
                if (!matches(e)) return;
                el.removeEventListener(endType, onEnd);
                el.removeEventListener(cancelType, onCancel);
                anim.__settle(false);
            }
            el.addEventListener(endType, onEnd);
            el.addEventListener(cancelType, onCancel);
        }

        function objectFor(entry) {
            var el = entry.el;
            var map = registry.get(el);
            if (!map) { map = Object.create(null); registry.set(el, map); }
            var isAnim = entry.prop === null;
            var isScript = isAnim && entry.run >= 1000;
            var key = isScript ? '@' + entry.run + ':' + entry.generation
                    : (isAnim ? '@' + entry.run + ':' + entry.name : entry.prop) + ':' + entry.generation;
            var existing = map[key];
            if (existing) return existing;
            if (isScript) return null;
            var anim = isAnim ? new CSSAnimation() : new CSSTransition();
            anim.__el = el;
            anim.__prop = isAnim ? '@' + entry.run : entry.prop;
            anim.__name = entry.name;
            anim.__gen = entry.generation;
            anim.__kind = isAnim ? 'animation' : 'transition';
            attachEndListeners(anim);
            map[key] = anim;
            return anim;
        }

        function collect(el) {
            var entries = global.__ns_anim_list(el || null);
            var out = [];
            for (var i = 0; i < entries.length; i++) {
                var obj = objectFor(entries[i]);
                if (obj) out.push(obj);
            }
            out.sort(function (a, b) {
                var rank = { transition: 0, animation: 1, script: 2 };
                if (a.__kind === 'script' || b.__kind === 'script') {
                    if (a.__kind !== b.__kind) return rank[a.__kind] - rank[b.__kind];
                    return a.__gen - b.__gen;
                }
                if (a.__el === b.__el) {
                    if (a.__kind !== b.__kind) return rank[a.__kind] - rank[b.__kind];
                    return 0;
                }
                var pos = a.__el.compareDocumentPosition(b.__el);
                return (pos & 4) ? -1 : (pos & 2) ? 1 : 0;
            });
            return out;
        }

        global.__ns_anim_script_event = function (el, run, kind) {
            var map = registry.get(el);
            if (!map) return;
            var prefix = '@' + run + ':';
            for (var key in map) {
                if (key.indexOf(prefix) !== 0) continue;
                var anim = map[key];
                if (kind === 'finish') anim.__settle(true);
                else if (kind === 'cancel' && !anim.__cancelling) anim.__settle(false);
            }
        };
        function elementAnimate(keyframes, options) {
            var effect = new KeyframeEffect(this, keyframes, options);
            var anim = new Animation(effect, typeof document !== 'undefined' ? document.timeline : null);
            if (options && typeof options === 'object' && options.id !== undefined) anim.id = String(options.id);
            anim.play();
            return anim;
        }
        try {
            if (global.Element) {
                Object.defineProperty(global.Element.prototype, 'animate', {
                    value: elementAnimate, writable: true, configurable: true
                });
            }
        } catch (e) {}
        function elementGetAnimations(options) {
            var self = this;
            if (options && options.subtree) {
                return collect(null).filter(function (a) {
                    return a.__el === self || (self.contains && self.contains(a.__el));
                });
            }
            return collect(this);
        }
        try {
            if (global.Element) {
                Object.defineProperty(global.Element.prototype, 'getAnimations', {
                    value: elementGetAnimations, writable: true, configurable: true
                });
            }
            if (typeof document !== 'undefined') {
                Object.defineProperty(Object.getPrototypeOf(document) || document, 'getAnimations', {
                    value: function () { return collect(null); }, writable: true, configurable: true
                });
            }
        } catch (e) {}
    })();

    (function () {
        var hrefDesc = global.Element &&
            Object.getOwnPropertyDescriptor(global.Element.prototype, 'href');
        if (!hrefDesc || typeof hrefDesc.get !== 'function') return;
        ['HTMLAnchorElement', 'HTMLAreaElement'].forEach(function (name) {
            var ctor = global[name];
            if (typeof ctor !== 'function' || !ctor.prototype) return;
            try {
                Object.defineProperty(ctor.prototype, 'toString', {
                    configurable: true, enumerable: true, writable: true,
                    value: function toString() {
                        if (!(this instanceof ctor))
                            throw new TypeError('Illegal invocation');
                        return hrefDesc.get.call(this);
                    }
                });
            } catch (e) {}
        });
    })();

})(typeof globalThis !== 'undefined' ? globalThis : this);
