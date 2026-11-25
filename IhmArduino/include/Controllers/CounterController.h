#pragma once

#include "Controllers/BaseController.h"
#include "Helpers/SubjectHelper.h"

//Status utilisé pour indiqué à l'IHM que le compteur a changer
CREATE_SUBJECT(kSubStatusCounter,Counter)

//Commande permettant de reinitialiser le compteur
CREATE_SUBJECT(kSubCmdResetCounter,reset_counter)


class CounterController : public BaseController {

public:
  struct __attribute__((packed)) Payload {
    uint32_t value;
  } counter = { 0 };

  /// @brief Constructeur de la classe CounterController
  CounterController();  

private:
  /// @brief Gère la commande de réinitialisation
  /// @param message message reçu
  void onCmdReset(const mps::FrameView& message);

  /// @brief Gère le timer du compteur, incrémente la valeur toutes les 2 secondes
  void handleCounterTimer();
  /// @brief Publie la valeur du compteur
  void publishCounter();

public:
  /// @brief Met à jour le contrôleur
  virtual void update() override;
  /// @brief Initialise le contrôleur
  virtual void setup() override;
  /// @brief Action à effectuer lors de l'ouverture de la session MPS
  virtual void onSessionOpened() override;
};