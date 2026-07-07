import { useEffect, useRef } from "react";
import { PrimitiveType } from "./engine";

// Kısayolların çağıracağı fonksiyonların tiplerini tanımlıyoruz
interface ShortcutHandlers {
  ready: boolean;
  onNew: () => void;
  onOpen: () => void;
  onSave: () => void;
  toggleGrid: () => void;
  removeSelected: () => void;
  addPrimitive: (type: PrimitiveType) => void;
}

export function useShortcuts(handlers: ShortcutHandlers) {
  // Fonksiyon referanslarını güncel tutmak için useRef kullanıyoruz.
  // Bu sayede useEffect içindeki event listener'ı sürekli sil-yükle yapmaktan kurtuluyoruz.
  const handlersRef = useRef(handlers);

  // Gelen fonksiyonlar her render'da değiştiğinde referansı güncelliyoruz
  useEffect(() => {
    handlersRef.current = handlers;
  }, [handlers]);

  useEffect(() => {
    const handleKeyDown = (event: KeyboardEvent) => {
      // Kullanıcı input alanlarında (renk veya sayı girerken) yazı yazıyorsa kısayolları iptal et
      const target = event.target as HTMLElement;
      if (target.tagName === "INPUT" || target.tagName === "TEXTAREA") {
        return;
      }

      const { ready, onOpen, onSave, toggleGrid, removeSelected, addPrimitive } = handlersRef.current;

      // Engine hazır değilse hiçbir kısayolu çalıştırma
      if (!ready) return;

      // Mac için metaKey (Cmd), Windows için ctrlKey
      const isCtrlOrCmd = event.ctrlKey || event.metaKey;

      if (isCtrlOrCmd) {
        switch (event.key.toLowerCase()) {
            case "o":
                event.preventDefault(); // Tarayıcının dosya açma menüsünü engelle
                onOpen();
                break;
            case "s":
                event.preventDefault(); // Sayfayı kaydetmeyi engelle
                onSave();
                break;
        }
      } else {
        // Ctrl/Cmd basılı olmayan tekil tuşlar
        switch (event.key.toLowerCase()) {
          case "delete":
          case "backspace":
            event.preventDefault();
            removeSelected();
            break;
          case "g":
            event.preventDefault();
            toggleGrid();
            break;
          // İsteğe bağlı: Obje eklemek için hızlı tuşlar atayabilirsin
          case "1":
            addPrimitive(PrimitiveType.Sphere);
            break;
          case "2":
            addPrimitive(PrimitiveType.Box);
            break;
        }
      }
    };

    window.addEventListener("keydown", handleKeyDown);
    return () => window.removeEventListener("keydown", handleKeyDown);
  }, []); // Boş bağımlılık dizisi: Event listener sadece ilk yüklemede eklenir
}