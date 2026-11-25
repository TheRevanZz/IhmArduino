## Subjects :
- ``status_sensor_d_counter``<br>
Ce subject permet d'indiquer que le compteur a changé. Le payload contient un ``uint32_t`` et fait 4 octets.

- ``cmd_sensor_d_reset_counter``<br>
Lorsque ce subject est reçu, le compteur est réinitialiser puis transmis à Godot via ``status_sensor_d_counter``.

- ``hb_sensor_d``<br>
Ce subject permet d'envoyer un heartbeat. Le payload contient un ``uint8_t`` et fait 7 octets.

- ``set_sensor_d_light_color``<br>
Lorsque ce subject est reçu, la couleur de la LED est changé. Le payload contient un array de 3 ``uint8_t`` et fait 3 octets.

- ``status_sensor_d_light_color``<br>
Ce subject permet d'indiquer que la couleur de la LED a changé. Le payload contient un array de 3 ``uint8_t`` et fait  3 octets.

- ``set_sensor_d_switch_threshold``<br>
Lorsque ce subject est reçu, la temps de maintien du bouton pour allumer la LED est changé. Le payload contient un ``uint8_t`` et fait 1 octet.

- ``status_sensor_d_switch_threshold``<br>
Ce subject permet d'indiquer que le temps de maintien a changé. Le payload contient un ``uint8_t`` et fait 1 octet.
