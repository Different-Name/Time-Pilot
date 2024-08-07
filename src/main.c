#include "main.h"

#include "../res/bullet_tiles.h"
#include "../res/ship_tiles.h"
#include "../res/sky_map.h"
#include "../res/sky_tiles.h"
#include "calc.h"

uint8_t joypad_state = 0;
uint8_t last_joypad_state = 0;
bool paused = false;

Player player;
GameObject enemies[ENEMY_COUNT];
GameObject player_bullets[PLAYER_BULLET_COUNT];
GameObject enemy_bullets[ENEMY_BULLET_COUNT];

Vector8 world_movement;

void main(void) {
	init_gfx();

	while (1) {
		joypad_state = joypad();

		if ((joypad_state & J_START) && !(last_joypad_state & J_START)) {
			paused = !paused;
		}

		if (!paused) {
			game_loop();
		}

		last_joypad_state = joypad_state;

		// Done processing, yield CPU and wait for start of next frame
		wait_vbl_done();
	}
}

void init_gfx(void) {
	// Load background tiles and then map
	set_bkg_data(0, 19u, sky_tiles);
	set_bkg_tiles(0, 0, sky_mapWidth, sky_mapHeight, sky_mapPLN0);

	// Load sprite tiles
	set_sprite_data(0, 16, ship_tiles);
	set_sprite_data(16 + 1, 1, bullet_tiles);

	OBP0_REG = 0xE0; // Updating sprite color palette to
					 // [black, dark grey, white, white]
					 // https://gbdev.gg8.se/forums/viewtopic.php?id=230
	OBP1_REG = 0x2C;

	// Load player
	player.gameObject.sprite_index = PLAYER_SPRITE_INDEX;
	player.gameObject.enabled = true;
	player.gameObject.position.x = PLAYER_X;
	player.gameObject.position.y = PLAYER_Y;

	set_sprite_tile(player.gameObject.sprite_index, 0);
	move_sprite(player.gameObject.sprite_index, PLAYER_X, PLAYER_Y);

	// Load enemies
	for (uint8_t i = 0; i < ENEMY_COUNT; i++) {
		GameObject* enemy = &enemies[i];

		enemy->sprite_index = ENEMY_SPRITE_INDEX + i;
		enemy->enabled = true;
		enemy->position.x = (i + 1) * 30;
		enemy->position.y = (i + 1) * 30;

		set_sprite_tile(enemy->sprite_index, 0);
		set_sprite_prop(enemy->sprite_index, S_PALETTE);
		move_sprite(enemy->sprite_index, enemy->position.x, enemy->position.y);
	}

	// Load player bullets
	for (uint8_t i = 0; i < PLAYER_BULLET_COUNT; i++) {
		GameObject* player_bullet = &player_bullets[i];

		player_bullet->sprite_index = BULLET_SPRITE_INDEX + i;
		player_bullet->enabled = false;
		player_bullet->position.x = 0;
		player_bullet->position.y = 0;

		set_sprite_tile(player_bullet->sprite_index, 16 + 1);
		move_sprite(player_bullet->sprite_index, player_bullets->position.x,
					player_bullet->position.y);
	}

	// Load player bullets
	for (uint8_t i = 0; i < ENEMY_BULLET_COUNT; i++) {
		GameObject* enemy_bullet = &enemy_bullets[i];

		enemy_bullet->sprite_index = BULLET_SPRITE_INDEX + PLAYER_BULLET_COUNT + i;
		enemy_bullet->enabled = false;
		enemy_bullet->position.x = 0;
		enemy_bullet->position.y = 0;

		set_sprite_tile(enemy_bullet->sprite_index, 16 + 1);
		set_sprite_prop(enemy_bullet->sprite_index, S_PALETTE);
		move_sprite(enemy_bullet->sprite_index, enemy_bullet->position.x, enemy_bullet->position.y);
	}

	// Turn the background map, sprites and display on
	SHOW_BKG;
	SHOW_SPRITES;
	DISPLAY_ON;
}

void game_loop(void) {
	update_player_rotation();
	update_player_position();

	for (uint8_t i = 0; i < ENEMY_COUNT; i++) {
		if (!enemies[i].enabled) continue; // Don't process enemy if disabled

		if ((((uint8_t)sys_time + i) & 3) == 0) { // Update rotation every 5 frames
			update_enemy_rotation(&enemies[i]);
		}
		if ((((uint8_t)sys_time + i) & 1) == 0) { // Update position every 2 frames
			update_enemy_position(&enemies[i]);
		}
		if (((sys_time + i * 64) & 255) == 0) { // Shoot every 256 frames
			spawn_bullet(&enemies[i], enemy_bullets, false);
		}
	}

	for (uint8_t i = 0; i < PLAYER_BULLET_COUNT; i++) {
		update_bullet_position(&player_bullets[i]);
	}
	for (uint8_t i = 0; i < ENEMY_BULLET_COUNT; i++) {
		update_bullet_position(&enemy_bullets[i]);
	}

	handle_player_firing();
}

void update_player_rotation(void) {
	// Only update player rotation every n frames
	if (player.rotation_counter < SHIP_ROTATION_THRESHOLD) {
		player.rotation_counter++;
		return;
	}

	// Retrieve current direction the dpad is held in, -1 represents no input
	int8_t target_direction = get_dpad_direction();
	if (target_direction == 16) return; // Out of range if dpad not pressed

	// The sprite_index represents the current rotation of the player
	player.gameObject.rotation = step_to_rotation(player.gameObject.rotation, target_direction);
	set_sprite_tile(player.gameObject.sprite_index, player.gameObject.rotation);

	// Reset frame counter
	player.rotation_counter = 0;
}

uint8_t get_dpad_direction(void) {
	if (joypad_state & J_UP) {
		if (joypad_state & J_LEFT) return 14;
		if (joypad_state & J_RIGHT) return 2;
		return 0;
	} else if (joypad_state & J_DOWN) {
		if (joypad_state & J_LEFT) return 10;
		if (joypad_state & J_RIGHT) return 6;
		return 8;
	} else if (joypad_state & J_LEFT) {
		return 12;
	} else if (joypad_state & J_RIGHT) {
		return 4;
	}

	return 16; // Out of range if dpad not pressed
}

void update_player_position(void) {
	// Scroll background by value calculated last frame
	// This is done because updating enemy positions is slower than updating the background pos
	scroll_bkg(world_movement.x, world_movement.y);

	movement_from_velocity(&(player.gameObject), &world_movement);

	for (uint8_t i = 0; i < ENEMY_COUNT; i++) {
		if (!enemies[i].enabled) {
			spawn_enemy();
			continue;
		};

		GameObject* enemy = &enemies[i];

		enemy->position.x -= world_movement.x;
		enemy->position.y -= world_movement.y;

		if (enemy->position.x > MAX_POS_X || enemy->position.y < MIN_POS_Y ||
			enemy->position.y > MAX_POS_Y) {
			enemy->enabled = false;
		}

		// We don't need to update the position of the sprite as it will be done later
		// Unless the enemy will be skipped this frame
		if (((sys_time + i) & 1) != 0) {
			move_sprite(enemies[i].sprite_index, enemies[i].position.x, enemies[i].position.y);
		}
	}

	for (uint8_t i = 0; i < PLAYER_BULLET_COUNT; i++) {
		if (!player_bullets[i].enabled) continue;
		player_bullets[i].position.x -= world_movement.x;
		player_bullets[i].position.y -= world_movement.y;
		// We don't need to update the position of the sprite as it will be done later
		// move_sprite(player_bullets[i].sprite_index, player_bullets[i].position.x,
		// 			player_bullets[i].position.y);
	}

	for (uint8_t i = 0; i < ENEMY_BULLET_COUNT; i++) {
		if (!enemy_bullets[i].enabled) continue;
		enemy_bullets[i].position.x -= world_movement.x;
		enemy_bullets[i].position.y -= world_movement.y;
		// We don't need to update the position of the sprite as it will be done later
		// move_sprite(enemy_bullets[i].sprite_index, enemy_bullets[i].position.x,
		// 			enemy_bullets[i].position.y);
	}
}

void spawn_enemy(void) {
	for (uint8_t i = 0; i < ENEMY_COUNT; i++) {
		if (enemies[i].enabled) continue;

		GameObject* enemy = &enemies[i];

		enemy->position.x = PLAYER_X;
		enemy->position.y = MIN_POS_Y + 10;
		enemy->rotation = 8;
		enemy->enabled = true;
	}
}

void update_enemy_rotation(GameObject* enemy) {
	if (!enemy->enabled) return;

	// Retrieve current direction the enemy is in
	int8_t target_direction = direction_to_point(&(enemy->position), &player.gameObject.position);

	// The sprite_index represents the current rotation of the enemy
	enemy->rotation = step_to_rotation(enemy->rotation, target_direction);
	set_sprite_tile(enemy->sprite_index, enemy->rotation);
}

void update_enemy_position(GameObject* enemy) {
	if (!enemy->enabled) return;

	Vector8 enemy_movement = {0, 0};
	movement_from_velocity(enemy, &enemy_movement);
	enemy->position.x += enemy_movement.x;
	enemy->position.y += enemy_movement.y;
	move_sprite(enemy->sprite_index, enemy->position.x, enemy->position.y);
}

void handle_player_firing(void) {
	if (player.fire_cooldown == 0 && joypad_state & J_A && !(last_joypad_state & J_A)) {
		spawn_bullet(&(player.gameObject), player_bullets, true);
		player.fire_cooldown = COOLDOWN_FRAMES;
	} else if (player.fire_cooldown > 0) {
		player.fire_cooldown--;
	}
}

void spawn_bullet(GameObject* ship, GameObject bullets[], bool friendly) {
	for (uint8_t i = 0; i < (friendly ? PLAYER_BULLET_COUNT : ENEMY_BULLET_COUNT); i++) {
		if (bullets[i].enabled) continue;

		bullets[i].rotation = ship->rotation;
		bullets[i].position.x = ship->position.x;
		bullets[i].position.y = ship->position.y;
		bullets[i].enabled = true;

		// We don't move the sprite so it will show next frame further from the ship
		return;
	}
}

void update_bullet_position(GameObject* bullet) {
	if (!bullet->enabled) return;

	Vector8 bullet_movement = {0, 0};
	movement_from_velocity(bullet, &bullet_movement);
	bullet->position.x += bullet_movement.x * 2;
	bullet->position.y += bullet_movement.y * 2;

	if (bullet->position.x > MAX_POS_X || bullet->position.y < MIN_POS_Y ||
		bullet->position.y > MAX_POS_Y) {
		bullet->enabled = false;
	}

	move_sprite(bullet->sprite_index, bullet->position.x, bullet->position.y);
}