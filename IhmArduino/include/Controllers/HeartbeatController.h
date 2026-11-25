#pragma once

#include "Controllers/BaseController.h"
#include "Helpers/SubjectHelper.h"

CREATE_SUBJECT(kSubHb,)

class HeartbeatController : public BaseController {

private:
  /// @brief Gère le heartbeat
  void handleHeartbeat();
  /// @brief Publie le heartbeat
  void publishHeartbeat();

public:
  /// @brief Update du contrôleur
  virtual void update() override;
  /// @brief Initialise le contrôleur
  virtual void setup() override {};
  /// @brief Action à effectuer lors de l'ouverture de la session MPS
  virtual void onSessionOpened() { publishHeartbeat(); }

};