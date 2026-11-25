#pragma once
#include <initializer_list>
#include "BaseController.h"
#include "SubjectHelper.h"
#include <vector>

// MAKE_SUBJECT(Cmd, Reset)
CREATE_SUBJECT(kSubCmdReset, Reset)

class ControllerManager {

public:
  /// @brief Constructeur de la classe ControllerManager
  /// @param _controllers liste des contrôleurs à gérer
  ControllerManager(std::initializer_list<BaseController*> _controllers) : 
    controllers{_controllers.begin(), _controllers.end()} {}

  
  /// @brief initialise le manager, exécuté au démarrage. Appelle la méthode setup() de tous les contrôleurs
  void setup();

  /// @brief effectue des actions à l'ouverture de la session MPS sur tous les contrôleurs
  void onSessionOpened();

  /// @brief Appelé dans la fonction loop() d'Arduino, exécute la méthode update() de tous les contrôleurs
  void loop();

  /// @brief Appelle la méthode reset() de tous les contrôleurs
  void reset();  

private:
  std::vector<BaseController*> controllers;
  
};