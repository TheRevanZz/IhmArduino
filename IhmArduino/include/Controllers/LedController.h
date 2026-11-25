#pragma once

#include <array>
#include "Helpers/SubjectHelper.h"
#include "Controllers/BaseController.h"
#include <MPS.h>

// Subject permettant de gérer la couleur de la led. Quand celle-ci est en noir, la led s'éteint
CREATE_SUBJECT(kSubSetLightColor, light_color)
// Status permettant d'informer Godot sur l'état de la lumière
CREATE_SUBJECT(kSubStatusLightColor, light_color)

// Subject Set permettant de gérer le nombre de secondes a maintenir le bouton pour allumer/éteindre la led
CREATE_SUBJECT(kSubSetSwitchThreshold, switch_threshold)
CREATE_SUBJECT(kSubStatusSwitchThreshold, switch_threshold)

class LedController : public BaseController
{

public:
	/// @brief Constructeur de la classe LedController
	LedController();

	// Contient les données concernant la lumière
	struct __attribute__((packed)) LedDataPayload
	{
		uint8_t r = 0;
		uint8_t g = 0;
		uint8_t b = 0;

		std::array<uint8_t, 3> color() const
		{
			std::array<uint8_t, 3> result{r, g, b};
			return result;
		}

	} ledDataPayload = {};
	
	// Contient le nombre de secondes a maintenir le bouton pour allumer/éteindre la led
	struct __attribute__((packed)) LedSwitchThresholdPayload
	{
		uint8_t switch_threshold = 5;
	} switchThresholdPayload;

private:
	uint8_t colorCursor = 0;
	bool holdingButton = false;
	uint8_t clickThreshold = 100;
	// En Millisecondes, le temps qu'il faut maintenir le bouton pour allumer/éteindre la led
	uint16_t switchLightThresholdMs = 5000;
	bool ignoreNextButtonRelease = false;

	/// @brief Action à effectuer lors de la réception d'un message pour changer la couleur de la led
	/// @param message Message contenant la nouvelle couleur de la led
	void onSetLightColor(const mps::FrameView &message);
	/// @brief Action à effectuer lors de la réception d'un message pour changer le seuil de basculement
	/// @param message Message contenant le nouveau seuil de basculement
	void onSetSwitchThreshold(const mps::FrameView &message);
	/// @brief Publie les données de la led
	void publishPayload();
	/// @brief Change la couleur de la led
	/// @param color Tableau contenant les valeurs RGB de la nouvelle couleur
	void changeLedColor(const uint8_t color[3]);
	/// @brief Change le seuil de basculement
	/// @param threshold Nouveau seuil de basculement
	void changeSwitchThreshold(uint8_t threshold);

	/// @brief Gère le contrôle de la led
	void lightControl();
	/// @brief Récupère l'index d'une couleur dans le tableau des couleurs
	/// @param color Tableau contenant les valeurs RGB de la couleur à rechercher
	/// @param valid Référence vers un booléen indiquant si la couleur est valide
	/// @return Index de la couleur dans le tableau des couleurs
	uint8_t getColorIndex(uint8_t color[3], bool &valid);
	/// @brief Met à jour le champ de couleur
	/// @param color Tableau contenant les valeurs RGB de la nouvelle couleur
	void updateColorField(const uint8_t color[3]);

public:
	/// @brief Met à jour le contrôleur
	virtual void update() override;
	/// @brief Initialise le contrôleur
	virtual void setup() override;
	/// @brief Action à effectuer lors de l'ouverture de la session MPS
	virtual void onSessionOpened() override;
};