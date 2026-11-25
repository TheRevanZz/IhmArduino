#pragma once

#include <MPS.h>

class BaseController 
{

protected:
	/// @brief Constructeur de la classe BaseController
	BaseController() {};

protected: 
  /// @brief Vérifie que la longueur du payload est correcte
  /// @param payload payload du message reçu
  /// @param length taille attendue du payload
  /// @return 
  bool verifyPayloadLength(const mps::FrameView& payload, uint8_t length);

  uint8_t payloadLength = 4;

public:
  /// @brief Action à effectuer lors de l'ouverture de la session MPS
  virtual void onSessionOpened(){}

  /// @brief Initialise le contrôleur
  virtual void setup() = 0;

  /// @brief Update du contrôleur
  virtual void update() = 0;

  /// @brief Réinitialise le contrôleur
  virtual void reset(){}
  
};