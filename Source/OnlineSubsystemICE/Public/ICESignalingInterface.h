// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "ICEAgent.h"

/**
 * Tipos de mensaje de señalización ICE
 */
enum class EICESignalType : uint8
{
	/** Oferta de sesión (candidatos del host) */
	Offer,
	/** Respuesta de sesión (candidatos del cliente) */
	Answer,
	/** Candidato ICE individual */
	Candidate
};

/**
 * Mensaje de señalización ICE
 */
struct FICESignalMessage
{
	/** Tipo de mensaje */
	EICESignalType Type;
	
	/** ID de sesión */
	FString SessionId;
	
	/** ID del remitente */
	FString SenderId;
	
	/** ID del destinatario (vacío para broadcast) */
	FString ReceiverId;
	
	/** Candidatos ICE */
	TArray<FICECandidate> Candidates;
	
	/** Datos adicionales del mensaje */
	TMap<FString, FString> Metadata;
	
	/** Timestamp del mensaje */
	FDateTime Timestamp;
	
	FICESignalMessage()
		: Type(EICESignalType::Candidate)
		, Timestamp(FDateTime::UtcNow())
	{}
	
	/** Convierte el mensaje a JSON */
	FString ToJson() const;
	
	/** Crea un mensaje desde JSON */
	static FICESignalMessage FromJson(const FString& JsonString);
};

/**
 * Delegate para notificar cuando se recibe un mensaje de señalización
 */
DECLARE_MULTICAST_DELEGATE_OneParam(FOnSignalMessageReceived, const FICESignalMessage&);

/**
 * Interfaz abstracta para mecanismos de señalización ICE
 * Permite intercambiar candidatos entre peers
 */
class IICESignaling
{
public:
	virtual ~IICESignaling() = default;
	
	/**
	 * Inicializar el sistema de señalización
	 * @return True si la inicialización fue exitosa
	 */
	virtual bool Initialize() = 0;
	
	/**
	 * Cerrar el sistema de señalización
	 */
	virtual void Shutdown() = 0;
	
	/**
	 * Enviar un mensaje de señalización
	 * @param Message - Mensaje a enviar
	 * @return True si el envío fue exitoso
	 */
	virtual bool SendSignal(const FICESignalMessage& Message) = 0;
	
	/**
	 * Procesar mensajes de señalización pendientes
	 * Debe llamarse periódicamente (tick)
	 */
	virtual void ProcessSignals() = 0;
	
	/**
	 * Verificar si el sistema de señalización está activo
	 * @return True si está conectado/activo
	 */
	virtual bool IsActive() const = 0;
	
	/**
	 * Obtener el ID único de este peer
	 * @return ID del peer local
	 */
	virtual FString GetLocalPeerId() const = 0;
	
	/**
	 * Registrar callback para mensajes recibidos
	 */
	FOnSignalMessageReceived OnSignalReceived;
};

/**
 * Implementación de señalización basada en archivos locales
 * Útil para testing local sin necesidad de servidor
 */
class FLocalFileSignaling : public IICESignaling
{
public:
	FLocalFileSignaling(const FString& SharedDirectory);
	virtual ~FLocalFileSignaling();
	
	// IICESignaling Interface
	virtual bool Initialize() override;
	virtual void Shutdown() override;
	virtual bool SendSignal(const FICESignalMessage& Message) override;
	virtual void ProcessSignals() override;
	virtual bool IsActive() const override;
	virtual FString GetLocalPeerId() const override;
	
private:
	/** Directorio compartido para archivos de señalización */
	FString SignalingDirectory;
	
	/** ID único de este peer */
	FString PeerId;
	
	/** Índice del último mensaje procesado */
	int32 LastProcessedMessageIndex;
	
	/** Si el sistema está activo */
	bool bIsActive;
	
	/** Crear directorio de señalización si no existe */
	bool EnsureSignalingDirectory();
	
	/** Generar nombre de archivo para mensaje */
	FString GenerateMessageFileName() const;
	
	/** Leer mensajes desde archivos */
	TArray<FICESignalMessage> ReadPendingMessages();
	
	/** Limpiar mensajes antiguos */
	void CleanupOldMessages();
};
