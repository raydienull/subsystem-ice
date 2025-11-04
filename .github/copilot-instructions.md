# Instrucciones de GitHub Copilot para el repositorio OnlineSubsystemICE

Idioma: Español (primario). Puedes responder en inglés técnico cuando el código o las APIs lo requieran, pero proporciona la explicación en español.

## Resumen breve del proyecto
OnlineSubsystemICE es un plugin de Unreal Engine que implementa una plataforma de "Online Subsystem" llamada ICE (Interactive Connectivity Establishment / servicio propio). Está escrito en C++ siguiendo las convenciones de Unreal Engine y contiene módulos bajo `Source/OnlineSubsystemICE` con código público y privado. El plugin se integra con el sistema de módulos de UE y está destinado a compilarse para Windows (Win64) dentro del editor Unreal.

Archivos y rutas clave:
- `OnlineSubsystemICE.uplugin` — metadatos del plugin.
- `Source/OnlineSubsystemICE/` — código fuente del plugin.
  - `Public/` — cabeceras públicas (API del plugin).
  - `Private/` — implementaciones.
- `Config/DefaultOnlineSubsystemICE.ini` — configuración por defecto.
- `Binaries/Win64/` e `Intermediate/Build/Win64/` — binarios y artefactos de compilación.

## Objetivo de estas instrucciones
- Proveer contexto del proyecto para que GitHub Copilot genere sugerencias más relevantes.
- Preferir soluciones que se integren con Unreal Engine 4/5 (según el target del repo), sigan las convenciones de rendimiento y seguridad, y respeten la arquitectura del subsystem.
- Evitar cambios grandes en la API pública sin una justificación clara en el prompt.

## Estilo de código y convenciones
- Sigue las convenciones de Unreal Engine C++: macros UCLASS/USTRUCT/UFUNCTION donde corresponda, uso de TSharedPtr/TUniquePtr/UE containers, y uso de F-prefijo para structs/clases (por ejemplo, `FICEAgent`).
- Nombres de archivos: `CamelCase` para clases, `ModuleName.h/.cpp` para módulos.
- Evita usar STL directo cuando existe un equivalente de UE (usar `TArray`, `FString`, `TMap`, etc.).
- Usa `UE_LOG(LogOnlineICE, Verbosity, TEXT("..."))` para registros. Si propones un nuevo log category, incluye la declaración `DEFINE_LOG_CATEGORY_STATIC` en el .cpp y la cabecera correspondiente.
- Los cambios que afecten al `Build.cs` deben explicar por qué se añaden dependencias y prefieren dependencias ya presentes en Unreal (no introducir librerías externas sin discusión).

## Qué incluir siempre en el prompt para obtener buenas sugerencias
- Ruta del archivo en el repo o fragmento relevante (p. ej. `Source/OnlineSubsystemICE/Private/ICEAgent.cpp`).
- Qué UE version o target (si lo conoces), p. ej. "UE4.26" o "UE5.4".
- El objetivo concreto: "añadir manejo de error", "optimizar uso de memoria", "implementar interfaz X".
- Requisitos de API pública: si la función es pública, dar el contrato esperado (parámetros, comportamiento ante errores, excepciones/noexcepciones).

Ejemplo de prompt claro:
"En `Source/OnlineSubsystemICE/Private/OnlineSessionInterfaceICE.cpp`, implementa `FOnlineSessionICE::CreateSession` para validar parámetros, registrar errores con `UE_LOG`, y devolver `false` en fallos. Sigue el estilo de las demás funciones en `OnlineSessionInterfaceICE.cpp` y no cambies la firma pública."

## Ejemplos de prompts útiles
- "Refactoriza `ICEAgent.cpp` para extraer la lógica de reconexión en una función privada reusable y añade comentarios Doxygen." 
- "Genera una implementación segura y con manejo de hilos para `StartICE` que use `AsyncTask` o `FRunnable` según corresponda." 
- "Añade tests básicos o casos de uso para `OnlineIdentityInterfaceICE` (describe qué validar: logins simulados, tokens nulos)."

## Recomendaciones y límites (Do / Don't)
- Do: Proponer cambios que respeten la arquitectura de módulos y no rompan la compatibilidad binaria sin discusión.
- Do: Priorizar claridad, manejo de errores, y uso de las utilidades de Unreal Engine.
- Don't: Añadir dependencias externas (librerías de terceros) sin mencionarlo explícitamente en el prompt.
- Don't: Reescribir grandes bloques sin dar un resumen antes y una estrategia de migración.

## Cómo validar una sugerencia generada por Copilot (checklist rápido)
1. Revisar que no se haya introducido STL en lugar de tipos de UE.
2. Verificar uso correcto de macros UPROPERTY/UFUNCTION si el código expone tipos a reflection.
3. Comprobar que los logs usan `UE_LOG` con categorías definidas.
4. Asegurarse de que no haya cambios en el `Build.cs` que añadan dependencias externas sin justificación.
5. Compilar el plugin en Windows (Win64) con el editor Unreal y ejecutar cualquier test manual básico.

## Notas adicionales para reviewers/humanos
- Si Copilot propone cambios en la API pública (`Public/`), pide siempre que incluya una nota de migración o un ejemplo de uso.
- Para cambios en la red o seguridad (por ejemplo, manejo de tokens), solicitar un pequeño plan de mitigación y pruebas.

---

Si quieres, puedo:
- Adaptar estas instrucciones para inglés.
- Añadir fragmentos de prompt adicionales específicos para tareas habituales (e.g., "agregar logging detallado", "añadir métricas de rendimiento").
- Crear el archivo en el repo (ya lo he hecho si confirmas que quieres que lo añada).

