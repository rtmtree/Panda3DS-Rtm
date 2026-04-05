import { useRef, useState, useEffect, useCallback } from 'react';

// Key mappings shown in the controls panel (mirrors src/panda_sdl/mappings.cpp)
const CONTROLS = [
  { keys: ['L', 'K'], label: 'A / B' },
  { keys: ['O', 'I'], label: 'X / Y' },
  { keys: ['Q', 'P'], label: 'L / R' },
  { keys: ['↑↓←→'], label: 'D-Pad' },
  { keys: ['W', 'A', 'S', 'D'], label: 'Circle Pad' },
  { keys: ['Enter'], label: 'Start' },
  { keys: ['Backspace'], label: 'Select' },
];

// Dynamically inject panda3ds.js from /public once and cache the factory.
let moduleFactoryPromise = null;

function loadEmscriptenModule() {
  if (moduleFactoryPromise) return moduleFactoryPromise;

  moduleFactoryPromise = new Promise((resolve, reject) => {
    if (window.createPanda3DS) {
      resolve(window.createPanda3DS);
      return;
    }
    const script = document.createElement('script');
    script.src = '/panda3ds.js';
    script.onload = () => resolve(window.createPanda3DS);
    script.onerror = () =>
      reject(new Error('panda3ds.js not found. Run build_wasm.sh first.'));
    document.head.appendChild(script);
  });

  return moduleFactoryPromise;
}

const FIXED_ROM_URL = 'http://localhost:8008/monhundemo.3ds';

export default function App() {
  const canvasRef = useRef(null);
  const moduleRef = useRef(null);
  const fileInputRef = useRef(null);

  const [status, setStatus] = useState('idle'); // idle | loading-wasm | ready | loading-rom | running | error
  const [errorMsg, setErrorMsg] = useState('');
  const [romName, setRomName] = useState('');

  // Shared helper: push a Uint8Array into the WASM heap and boot the ROM.
  const loadRomBytes = useCallback(async (data, name) => {
    const mod = moduleRef.current;
    if (!mod) return;

    setStatus('loading-rom');
    setRomName(name);

    const ptr = mod._malloc(data.length);
    mod.HEAPU8.set(data, ptr);
    mod._loadROMData(ptr, data.length);
    mod._free(ptr);

    await new Promise((r) => setTimeout(r, 200));

    if (mod._isROMLoaded()) {
      setStatus('running');
    } else {
      setErrorMsg('ROM was not accepted by the emulator. Make sure it is a valid .3ds / .cci / .3dsx / .elf file.');
      setStatus('error');
    }
  }, []);

  // Initialise the WASM module once the canvas is mounted.
  // If ?loadFixedRom=true is in the URL, fetch and boot the fixed ROM automatically.
  useEffect(() => {
    let cancelled = false;

    async function init() {
      setStatus('loading-wasm');
      try {
        const factory = await loadEmscriptenModule();
        if (cancelled) return;

        const mod = await factory({
          canvas: canvasRef.current,
          // Suppress the default "Downloading…" overlay Emscripten draws
          setStatus: () => {},
          print:    (msg) => console.log('[panda3ds]', msg),
          printErr: (msg) => console.warn('[panda3ds]', msg),
        });

        if (cancelled) return;
        moduleRef.current = mod;

        // Auto-load fixed ROM when ?loadFixedRom=true
        const params = new URLSearchParams(window.location.search);
        if (params.get('loadFixedRom') === 'true') {
          setStatus('loading-rom');
          setRomName('monhundemo.3ds');
          try {
            const res = await fetch(FIXED_ROM_URL);
            if (!res.ok) throw new Error(`Server returned ${res.status} for ${FIXED_ROM_URL}`);
            const data = new Uint8Array(await res.arrayBuffer());
            if (cancelled) return;
            await loadRomBytes(data, 'monhun.3ds');
          } catch (err) {
            if (!cancelled) {
              setErrorMsg(`Failed to fetch fixed ROM: ${err.message}`);
              setStatus('error');
            }
          }
        } else {
          setStatus('ready');
        }
      } catch (err) {
        if (!cancelled) {
          setErrorMsg(err.message);
          setStatus('error');
        }
      }
    }

    init();
    return () => { cancelled = true; };
  }, [loadRomBytes]);

  // Handle file selection from the <input type="file">
  const handleFileChange = useCallback(async (e) => {
    const file = e.target.files?.[0];
    if (!file || !moduleRef.current) return;

    try {
      const data = new Uint8Array(await file.arrayBuffer());
      await loadRomBytes(data, file.name);
    } catch (err) {
      setErrorMsg(err.message);
      setStatus('error');
    }

    // Reset input so the same file can be loaded again if needed
    e.target.value = '';
  }, [loadRomBytes]);

  const openFilePicker = () => fileInputRef.current?.click();

  const canLoad = status === 'ready' || status === 'running' || status === 'error';

  return (
    <div className="app">
      {/* ── header ── */}
      <header className="app-header">
        <div className="logo">
          <span className="logo-icon">🐼</span>
          <span className="logo-text">Panda3DS</span>
          <span className="logo-sub">Web Emulator</span>
        </div>

        <div className="header-actions">
          <StatusBadge status={status} romName={romName} />

          <button
            className="btn-primary"
            onClick={openFilePicker}
            disabled={!canLoad}
            title={
              status === 'loading-wasm'
                ? 'Waiting for emulator to initialise…'
                : 'Load a 3DS ROM (.3ds .cci .3dsx .elf)'
            }
          >
            {status === 'running' ? '⏏ Change ROM' : '📂 Import ROM'}
          </button>

          {/* Hidden file input */}
          <input
            ref={fileInputRef}
            type="file"
            accept=".3ds,.cci,.3dsx,.elf,.cxi"
            style={{ display: 'none' }}
            onChange={handleFileChange}
          />
        </div>
      </header>

      {/* ── main content ── */}
      <main className="app-main">
        {/* Emulator canvas */}
        <div className="canvas-wrapper">
          <canvas
            id="canvas"
            ref={canvasRef}
            width={400}
            height={480}
            className="emu-canvas"
            tabIndex={0}
          />

          {/* Overlays shown before / instead of the canvas */}
          {status === 'loading-wasm' && (
            <div className="canvas-overlay">
              <Spinner />
              <p>Loading emulator…</p>
            </div>
          )}

          {status === 'idle' && (
            <div className="canvas-overlay">
              <p className="hint">Initialising…</p>
            </div>
          )}

          {status === 'ready' && (
            <div className="canvas-overlay clickable" onClick={openFilePicker}>
              <div className="drop-hint">
                <span className="drop-icon">📁</span>
                <p>Click to load a ROM</p>
                <p className="hint">.3ds  .cci  .3dsx  .elf</p>
              </div>
            </div>
          )}

          {status === 'loading-rom' && (
            <div className="canvas-overlay">
              <Spinner />
              <p>Loading {romName}…</p>
            </div>
          )}

          {status === 'error' && (
            <div className="canvas-overlay error-overlay">
              <span className="error-icon">⚠️</span>
              <p className="error-title">Error</p>
              <p className="error-msg">{errorMsg}</p>
              {canLoad && (
                <button className="btn-primary" onClick={openFilePicker}>
                  Try another ROM
                </button>
              )}
            </div>
          )}
        </div>

        {/* Controls reference */}
        <aside className="controls-panel">
          <h3>Controls</h3>
          <table className="controls-table">
            <tbody>
              {CONTROLS.map(({ keys, label }) => (
                <tr key={label}>
                  <td>
                    {keys.map((k) => (
                      <kbd key={k}>{k}</kbd>
                    ))}
                  </td>
                  <td>{label}</td>
                </tr>
              ))}
            </tbody>
          </table>

          <div className="controls-note">
            <p>Click the bottom screen to send touch input.</p>
          </div>
        </aside>
      </main>
    </div>
  );
}

// ── sub-components ────────────────────────────────────────────────────────────

function Spinner() {
  return <div className="spinner" aria-label="Loading" />;
}

function StatusBadge({ status, romName }) {
  const labels = {
    idle:        { text: 'Initialising',   cls: 'badge-idle'    },
    'loading-wasm': { text: 'Loading WASM', cls: 'badge-loading' },
    ready:       { text: 'Ready',          cls: 'badge-ready'   },
    'loading-rom':  { text: 'Loading ROM', cls: 'badge-loading' },
    running:     { text: romName || 'Running', cls: 'badge-running' },
    error:       { text: 'Error',          cls: 'badge-error'   },
  };
  const { text, cls } = labels[status] ?? labels.idle;
  return <span className={`badge ${cls}`}>{text}</span>;
}
