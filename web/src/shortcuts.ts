import { useEffect, useRef } from "react";
import { PrimitiveType } from "./engine";

// Kısayolların çağıracağı fonksiyonların tiplerini tanımlıyoruz.
// Bu dosyada engine/scene logic yok; sadece keyboard event -> handler çağrısı var.
interface ShortcutHandlers {
    ready: boolean;

    onNew: () => void;
    onOpen: () => void;
    onSave: () => void;

    onUndo: () => void;
    onRedo: () => void;

    toggleGrid: () => void;
    removeSelected: () => void;
    addPrimitive: (type: PrimitiveType) => void;

    // Esc ile aktif gizmo drag işlemini iptal etmek için.
    cancelGizmoDrag?: () => void;
}

function isEditableTarget(target: EventTarget | null): boolean {
    if (!(target instanceof HTMLElement)) {
        return false;
    }

    const tagName = target.tagName.toLowerCase();

    if (
        tagName === "input" ||
        tagName === "textarea" ||
        tagName === "select"
    ) {
        return true;
    }

    if (target.isContentEditable) {
        return true;
    }

    // İç içe contenteditable alanlar için.
    if (target.closest("[contenteditable='true']")) {
        return true;
    }

    return false;
}

export function useShortcuts(handlers: ShortcutHandlers) {
    // Fonksiyon referanslarını güncel tutmak için useRef kullanıyoruz.
    // Bu sayede useEffect içindeki event listener'ı sürekli sil-yükle yapmaktan kurtuluyoruz.
    const handlersRef = useRef(handlers);

    // Gelen fonksiyonlar her render'da değiştiğinde referansı güncelliyoruz.
    useEffect(() => {
        handlersRef.current = handlers;
    }, [handlers]);

    useEffect(() => {
        const handleKeyDown = (event: KeyboardEvent) => {
            // Kullanıcı input alanlarında yazı yazıyorsa engine shortcut çalıştırma.
            // Böylece input içindeyken Ctrl+Z normal text undo olarak kalır.
            if (isEditableTarget(event.target)) {
                return;
            }

            const {
                ready,
                onOpen,
                onSave,
                onUndo,
                onRedo,
                toggleGrid,
                removeSelected,
                addPrimitive,
                cancelGizmoDrag,
            } = handlersRef.current;

            // Engine hazır değilse hiçbir kısayolu çalıştırma.
            if (!ready) return;

            // Mac için metaKey / Cmd, Windows-Linux için ctrlKey.
            const isCtrlOrCmd = event.ctrlKey || event.metaKey;
            const key = event.key.toLowerCase();

            if (isCtrlOrCmd) {
                switch (key) {
                    case "z":
                        event.preventDefault();

                        if (event.shiftKey) {
                            onRedo();
                        } else {
                            onUndo();
                        }

                        break;

                    case "y":
                        event.preventDefault();
                        onRedo();
                        break;

                    case "o":
                        event.preventDefault();
                        onOpen();
                        break;

                    case "s":
                        event.preventDefault();
                        onSave();
                        break;

                    default:
                        break;
                }

                return;
            }

            // Ctrl/Cmd basılı olmayan tekil tuşlar.
            switch (key) {
                case "escape":
                    event.preventDefault();
                    cancelGizmoDrag?.();
                    break;

                case "delete":
                case "backspace":
                    event.preventDefault();
                    removeSelected();
                    break;

                case "g":
                    event.preventDefault();
                    toggleGrid();
                    break;

                case "1":
                    event.preventDefault();
                    addPrimitive(PrimitiveType.Sphere);
                    break;

                case "2":
                    event.preventDefault();
                    addPrimitive(PrimitiveType.Box);
                    break;

                default:
                    break;
            }
        };

        window.addEventListener("keydown", handleKeyDown);

        return () => {
            window.removeEventListener("keydown", handleKeyDown);
        };
    }, []);
}