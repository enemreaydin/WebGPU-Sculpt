import ReactDOM from "react-dom/client";
import App from "./App";
import "./index.css";

// NOTE: StrictMode is intentionally omitted. In development it double-invokes
// mount effects, which would instantiate the WASM engine (and its WebGPU
// device + render loop) twice on the same canvas, causing device-mismatch
// validation errors.
ReactDOM.createRoot(document.getElementById("root")!).render(<App />);
