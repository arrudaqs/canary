/**
 * Canary - A free and open-source MMORPG server emulator
 * Copyright (©) 2019-2022 OpenTibiaBR <opentibiabr@outlook.com>
 * Repository: https://github.com/opentibiabr/canary
 * License: https://github.com/opentibiabr/canary/blob/main/LICENSE
 * Contributors: https://github.com/opentibiabr/canary/graphs/contributors
 * Website: https://docs.opentibiabr.com/
 */

#include "io/functions/iologindata_save_player.hpp"

#include "config/configmanager.hpp"
#include "creatures/combat/condition.hpp"
#include "creatures/monsters/monsters.hpp"
#include "game/game.hpp"
#include "io/ioprey.hpp"
#include "items/containers/depot/depotchest.hpp"
#include "items/containers/inbox/inbox.hpp"
#include "items/containers/rewards/reward.hpp"

bool IOLoginDataSave::saveItems(const std::shared_ptr<Player> &player, const ItemBlockList &items, DBInsert &query_insert, PropWriteStream &propWriteStream) {
	if (!player) {
		g_logger().warn("[IOLoginData::savePlayer] - Player nullptr: {}", __FUNCTION__);
		return false;
	}

	int32_t runningId = 100;

	using ContainerBlock = std::pair<std::shared_ptr<Container>, int32_t>;
	std::deque<ContainerBlock> queue;

	const auto &openContainers = player->getOpenContainers();
	const bool hasOpenContainers = !openContainers.empty();
	const uint32_t playerGUID = player->getGUID();

	for (const auto &it : items) {
		const auto &item = it.second;
		if (!item) {
			continue;
		}

		int32_t pid = it.first;
		++runningId;

		if (const std::shared_ptr<Container> &container = item->getContainer()) {
			if (!container) {
				continue;
			}

			if (container->getAttribute<int64_t>(ItemAttribute_t::OPENCONTAINER) > 0) {
				container->setAttribute(ItemAttribute_t::OPENCONTAINER, 0);
			}

			if (hasOpenContainers) {
				for (const auto &its : openContainers) {
					const auto &openContainer = its.second;
					const auto &opcontainer = openContainer.container;

					if (opcontainer == container) {
						container->setAttribute(ItemAttribute_t::OPENCONTAINER, static_cast<int>(its.first) + 1);
						break;
					}
				}
			}

			queue.emplace_back(container, runningId);
		}

		propWriteStream.clear();
		item->serializeAttr(propWriteStream);

		size_t attributesSize;
		const char* attributes = propWriteStream.getStream(attributesSize);
		auto escapedAttributes = g_database().escapeBlob(attributes, static_cast<uint32_t>(attributesSize));

		auto row = fmt::format("{},{},{},{},{},{}", playerGUID, pid, runningId, item->getID(), item->getSubType(), escapedAttributes);

		if (!query_insert.addRow(row)) {
			g_logger().error("Error adding row to query.");
			return false;
		}
	}

	while (!queue.empty()) {
		const ContainerBlock &cb = queue.front();
		const std::shared_ptr<Container> &container = cb.first;
		if (!container) {
			queue.pop_front();
			continue;
		}

		int32_t parentId = cb.second;

		for (const auto &item : container->getItemList()) {
			if (!item) {
				continue;
			}

			++runningId;

			if (const auto &subContainer = item->getContainer()) {
				queue.emplace_back(subContainer, runningId);
				if (subContainer->getAttribute<int64_t>(ItemAttribute_t::OPENCONTAINER) > 0) {
					subContainer->setAttribute(ItemAttribute_t::OPENCONTAINER, 0);
				}

				if (hasOpenContainers) {
					for (const auto &it : openContainers) {
						const auto &openContainer = it.second;
						const auto &opcontainer = openContainer.container;

						if (opcontainer == subContainer) {
							subContainer->setAttribute(ItemAttribute_t::OPENCONTAINER, it.first + 1);
							break;
						}
					}
				}
			}

			propWriteStream.clear();
			item->serializeAttr(propWriteStream);

			size_t attributesSize;
			const char* attributes = propWriteStream.getStream(attributesSize);
			auto escapedAttributes = g_database().escapeBlob(attributes, static_cast<uint32_t>(attributesSize));

			auto row = fmt::format("{},{},{},{},{},{}", playerGUID, parentId, runningId, item->getID(), item->getSubType(), escapedAttributes);
			if (!query_insert.addRow(row)) {
				g_logger().error("Error adding row to query for container item.");
				return false;
			}
		}

		// Removes the object after processing everything, avoiding memory usage after freeing
		queue.pop_front();
	}

	if (!query_insert.execute()) {
		g_logger().error("Error executing query.");
		return false;
	}

	return true;
}

bool IOLoginDataSave::savePlayerFirst(const std::shared_ptr<Player> &player) {
	if (!player) {
		g_logger().warn("[IOLoginDataSave::savePlayerFirst] - Player nullptr: {}", __FUNCTION__);
		return false;
	}

	if (player->getHealth() <= 0) {
		player->changeHealth(1);
	}

	// Check if `save` flag is set
	auto result = g_database().storeQuery(fmt::format("SELECT `save` FROM `players` WHERE `id` = {}", player->getGUID()));
	if (!result) {
		g_logger().warn("[IOLoginDataSave::savePlayerFirst] - Error retrieving save flag for player: {}", player->getName());
		return false;
	}

	// Quick update if `save` flag is 0
	if (result->getNumber<uint16_t>("save") == 0) {
		return g_database().executeQuery(fmt::format(
			"UPDATE `players` SET `lastlogin` = {}, `lastip` = {} WHERE `id` = {}",
			player->lastLoginSaved, player->lastIP, player->getGUID()
		));
	}

	// Build the list of column-value pairs
	std::vector<std::string> columns;
	columns.reserve(result->countColumns());

	// Basic Player Information
	columns.push_back(fmt::format("`name` = {}", g_database().escapeString(player->name)));
	columns.push_back(fmt::format("`level` = {}", player->level));
	columns.push_back(fmt::format("`group_id` = {}", player->group->id));
	columns.push_back(fmt::format("`vocation` = {}", player->getVocationId()));
	columns.push_back(fmt::format("`health` = {}", player->health));
	columns.push_back(fmt::format("`healthmax` = {}", player->healthMax));
	columns.push_back(fmt::format("`experience` = {}", player->experience));

	// Appearance Attributes
	columns.push_back(fmt::format("`lookbody` = {}", static_cast<uint32_t>(player->defaultOutfit.lookBody)));
	columns.push_back(fmt::format("`lookfeet` = {}", static_cast<uint32_t>(player->defaultOutfit.lookFeet)));
	columns.push_back(fmt::format("`lookhead` = {}", static_cast<uint32_t>(player->defaultOutfit.lookHead)));
	columns.push_back(fmt::format("`looklegs` = {}", static_cast<uint32_t>(player->defaultOutfit.lookLegs)));
	columns.push_back(fmt::format("`looktype` = {}", player->defaultOutfit.lookType));
	columns.push_back(fmt::format("`lookaddons` = {}", static_cast<uint32_t>(player->defaultOutfit.lookAddons)));
	columns.push_back(fmt::format("`lookmountbody` = {}", static_cast<uint32_t>(player->defaultOutfit.lookMountBody)));
	columns.push_back(fmt::format("`lookmountfeet` = {}", static_cast<uint32_t>(player->defaultOutfit.lookMountFeet)));
	columns.push_back(fmt::format("`lookmounthead` = {}", static_cast<uint32_t>(player->defaultOutfit.lookMountHead)));
	columns.push_back(fmt::format("`lookmountlegs` = {}", static_cast<uint32_t>(player->defaultOutfit.lookMountLegs)));
	columns.push_back(fmt::format("`lookfamiliarstype` = {}", player->defaultOutfit.lookFamiliarsType));

	columns.push_back(fmt::format("`isreward` = {}", static_cast<uint16_t>(player->isDailyReward)));
	columns.push_back(fmt::format("`maglevel` = {}", player->magLevel));
	columns.push_back(fmt::format("`mana` = {}", player->mana));

	// Gameplay Stats
	columns.push_back(fmt::format("`manamax` = {}", player->manaMax));
	columns.push_back(fmt::format("`manaspent` = {}", player->manaSpent));
	columns.push_back(fmt::format("`soul` = {}", static_cast<uint16_t>(player->soul)));
	columns.push_back(fmt::format("`town_id` = {}", player->town->getID()));
	columns.push_back(fmt::format("`posx` = {}", player->getLoginPosition().getX()));
	columns.push_back(fmt::format("`posy` = {}", player->getLoginPosition().getY()));
	columns.push_back(fmt::format("`posz` = {}", player->getLoginPosition().getZ()));
	columns.push_back(fmt::format("`prey_wildcard` = {}", player->getPreyCards()));
	columns.push_back(fmt::format("`task_points` = {}", player->getTaskHuntingPoints()));
	columns.push_back(fmt::format("`boss_points` = {}", player->getBossPoints()));
	columns.push_back(fmt::format("`forge_dusts` = {}", player->getForgeDusts()));
	columns.push_back(fmt::format("`forge_dust_level` = {}", player->getForgeDustLevel()));
	columns.push_back(fmt::format("`randomize_mount` = {}", static_cast<uint16_t>(player->isRandomMounted())));
	columns.push_back(fmt::format("`cap` = {}", (player->capacity / 100)));
	columns.push_back(fmt::format("`sex` = {}", static_cast<uint16_t>(player->sex)));
	if (player->lastLoginSaved != 0) {
		columns.push_back(fmt::format("`lastlogin` = {}", player->lastLoginSaved));
	}
	if (player->lastIP) {
		columns.push_back(fmt::format("`lastip` = {}", player->lastIP));
	}

	// Serialize conditions
	PropWriteStream propWriteStream;
	for (const auto &condition : player->conditions) {
		if (condition->isPersistent()) {
			condition->serialize(propWriteStream);
			propWriteStream.write<uint8_t>(CONDITIONATTR_END);
		}
	}

	size_t attributesSize;
	const char* attributes = propWriteStream.getStream(attributesSize);
	if (attributesSize) {
		columns.push_back(fmt::format("`conditions` = {}", g_database().escapeBlob(attributes, static_cast<uint32_t>(attributesSize))));
	}

	// Skull attributes, based on world type
	if (g_game().getWorldType() != WORLD_TYPE_PVP_ENFORCED) {
		int64_t skullTime = 0;
		if (player->skullTicks > 0) {
			auto now = std::chrono::system_clock::now();
			skullTime = std::chrono::duration_cast<std::chrono::seconds>(now.time_since_epoch()).count() + player->skullTicks;
		}
		columns.push_back(fmt::format("`skulltime` = {}", skullTime));
		columns.push_back(fmt::format("`skull` = {}", static_cast<int64_t>(player->skull)));
	}

	// Additional fields
	columns.push_back(fmt::format("`lastlogout` = {}", player->getLastLogout()));
	columns.push_back(fmt::format("`balance` = {}", player->bankBalance));
	columns.push_back(fmt::format("`offlinetraining_time` = {}", player->getOfflineTrainingTime() / 1000));
	columns.push_back(fmt::format("`offlinetraining_skill` = {}", player->getOfflineTrainingSkill()));
	columns.push_back(fmt::format("`skill_fist` = {}", player->skills[SKILL_FIST].level));
	columns.push_back(fmt::format("`skill_fist_tries` = {}", player->skills[SKILL_FIST].tries));
	columns.push_back(fmt::format("`skill_club` = {}", player->skills[SKILL_CLUB].level));
	columns.push_back(fmt::format("`skill_club_tries` = {}", player->skills[SKILL_CLUB].tries));
	columns.push_back(fmt::format("`skill_sword` = {}", player->skills[SKILL_SWORD].level));
	columns.push_back(fmt::format("`skill_sword_tries` = {}", player->skills[SKILL_SWORD].tries));
	columns.push_back(fmt::format("`skill_axe` = {}", player->skills[SKILL_AXE].level));
	columns.push_back(fmt::format("`skill_axe_tries` = {}", player->skills[SKILL_AXE].tries));
	columns.push_back(fmt::format("`skill_dist` = {}", player->skills[SKILL_DISTANCE].level));
	columns.push_back(fmt::format("`skill_dist_tries` = {}", player->skills[SKILL_DISTANCE].tries));
	columns.push_back(fmt::format("`skill_shielding` = {}", player->skills[SKILL_SHIELD].level));
	columns.push_back(fmt::format("`skill_shielding_tries` = {}", player->skills[SKILL_SHIELD].tries));
	columns.push_back(fmt::format("`skill_fishing` = {}", player->skills[SKILL_FISHING].level));
	columns.push_back(fmt::format("`skill_fishing_tries` = {}", player->skills[SKILL_FISHING].tries));
	columns.push_back(fmt::format("`skill_critical_hit_chance` = {}", player->skills[SKILL_CRITICAL_HIT_CHANCE].level));
	columns.push_back(fmt::format("`skill_critical_hit_chance_tries` = {}", player->skills[SKILL_CRITICAL_HIT_CHANCE].tries));
	columns.push_back(fmt::format("`skill_critical_hit_damage` = {}", player->skills[SKILL_CRITICAL_HIT_DAMAGE].level));
	columns.push_back(fmt::format("`skill_critical_hit_damage_tries` = {}", player->skills[SKILL_CRITICAL_HIT_DAMAGE].tries));
	columns.push_back(fmt::format("`skill_life_leech_chance` = {}", player->skills[SKILL_LIFE_LEECH_CHANCE].level));
	columns.push_back(fmt::format("`skill_life_leech_chance_tries` = {}", player->skills[SKILL_LIFE_LEECH_CHANCE].tries));
	columns.push_back(fmt::format("`skill_life_leech_amount` = {}", player->skills[SKILL_LIFE_LEECH_AMOUNT].level));
	columns.push_back(fmt::format("`skill_life_leech_amount_tries` = {}", player->skills[SKILL_LIFE_LEECH_AMOUNT].tries));
	columns.push_back(fmt::format("`skill_mana_leech_chance` = {}", player->skills[SKILL_MANA_LEECH_CHANCE].level));
	columns.push_back(fmt::format("`skill_mana_leech_chance_tries` = {}", player->skills[SKILL_MANA_LEECH_CHANCE].tries));
	columns.push_back(fmt::format("`skill_mana_leech_amount` = {}", player->skills[SKILL_MANA_LEECH_AMOUNT].level));
	columns.push_back(fmt::format("`skill_mana_leech_amount_tries` = {}", player->skills[SKILL_MANA_LEECH_AMOUNT].tries));
	columns.push_back(fmt::format("`stamina` = {}", player->getStaminaMinutes()));
	columns.push_back(fmt::format("`manashield` = {}", player->getManaShield()));
	columns.push_back(fmt::format("`max_manashield` = {}", player->getMaxManaShield()));
	columns.push_back(fmt::format("`xpboost_value` = {}", player->getXpBoostPercent()));
	columns.push_back(fmt::format("`xpboost_stamina` = {}", player->getXpBoostTime()));
	columns.push_back(fmt::format("`quickloot_fallback` = {}", player->quickLootFallbackToMainContainer ? 1 : 0));

	// Blessings
	for (int i = 1; i <= 8; ++i) {
		columns.push_back(fmt::format("`blessings{}` = {}", i, static_cast<uint32_t>(player->getBlessingCount(static_cast<uint8_t>(i)))));
	}

	// Now join the columns into a single string
	std::string setClause = fmt::to_string(fmt::join(columns, ", "));

	// Construct the final query
	std::string queryStr = fmt::format("UPDATE `players` SET {} WHERE `id` = {}", setClause, player->getGUID());

	// Execute the query
	if (!g_database().executeQuery(queryStr)) {
		g_logger().warn("[IOLoginDataSave::savePlayerFirst] - Error executing player first save for player: {}", player->getName());
		return false;
	}

	return true;
}

bool IOLoginDataSave::savePlayerSpells(const std::shared_ptr<Player> &player) {
	if (!player) {
		g_logger().warn("[IOLoginDataSave::savePlayerSpells] - Player nullptr: {}", __FUNCTION__);
		return false;
	}

	// Use UPSERT to avoid the DELETE operation
	DBInsert spellsQuery("INSERT INTO `player_spells` (`player_id`, `name`) VALUES ");
	spellsQuery.upsert({ "name" });

	// Populate spells list with player's learned spells
	auto playerGUID = player->getGUID();
	for (const std::string &spellName : player->learnedInstantSpellList) {
		auto row = fmt::format("{}, {}", playerGUID, g_database().escapeString(spellName));
		if (!spellsQuery.addRow(row)) {
			g_logger().warn("[IOLoginDataSave::savePlayerSpells] - Failed to add spell data for player: {}", player->getName());
			return false;
		}
	}

	if (!spellsQuery.execute()) {
		g_logger().warn("[IOLoginDataSave::savePlayerSpells] - Error executing spells data insertion for player: {}", player->getName());
		return false;
	}

	return true;
}

bool IOLoginDataSave::savePlayerKills(const std::shared_ptr<Player> &player) {
	if (!player) {
		g_logger().warn("[IOLoginDataSave::savePlayerKills] - Player nullptr: {}", __FUNCTION__);
		return false;
	}

	DBInsert killsQuery("INSERT INTO `player_kills` (`player_id`, `target`, `time`, `unavenged`) VALUES ");
	killsQuery.upsert({ "target", "time", "unavenged" });

	// Add rows for each kill entry
	auto playerGUID = player->getGUID();
	for (const auto &kill : player->unjustifiedKills) {
		auto row = fmt::format("{}, {}, {}, {}", playerGUID, kill.target, kill.time, kill.unavenged);
		if (!killsQuery.addRow(row)) {
			g_logger().warn("[IOLoginDataSave::savePlayerKills] - Failed to add kill data for player: {}", player->getName());
			return false;
		}
	}

	if (!killsQuery.execute()) {
		g_logger().warn("[IOLoginDataSave::savePlayerKills] - Error executing kills data insertion for player: {}", player->getName());
		return false;
	}

	return true;
}

bool IOLoginDataSave::savePlayerBestiarySystem(const std::shared_ptr<Player> &player) {
	if (!player) {
		g_logger().warn("[IOLoginDataSave::savePlayerBestiarySystem] - Player nullptr: {}", __FUNCTION__);
		return false;
	}

	// Serialize the bestiary tracker list
	PropWriteStream propBestiaryStream;
	for (const auto &trackedType : player->getCyclopediaMonsterTrackerSet(false)) {
		propBestiaryStream.write<uint16_t>(trackedType->info.raceid);
	}
	size_t trackerSize;
	const char* trackerList = propBestiaryStream.getStream(trackerSize);
	auto escapedTrackerList = g_database().escapeBlob(trackerList, static_cast<uint32_t>(trackerSize));

	// Construct the query using fmt::format
	std::string updateQuery = fmt::format(
		"UPDATE `player_charms` SET `charm_points` = {}, `charm_expansion` = {}, "
		"`rune_wound` = {}, `rune_enflame` = {}, `rune_poison` = {}, `rune_freeze` = {}, "
		"`rune_zap` = {}, `rune_curse` = {}, `rune_cripple` = {}, `rune_parry` = {}, "
		"`rune_dodge` = {}, `rune_adrenaline` = {}, `rune_numb` = {}, `rune_cleanse` = {}, "
		"`rune_bless` = {}, `rune_scavenge` = {}, `rune_gut` = {}, `rune_low_blow` = {}, "
		"`rune_divine` = {}, `rune_vamp` = {}, `rune_void` = {}, `UsedRunesBit` = {}, "
		"`UnlockedRunesBit` = {}, `tracker list` = {} WHERE `player_guid` = {}",
		player->charmPoints,
		player->charmExpansion ? 1 : 0,
		player->charmRuneWound,
		player->charmRuneEnflame,
		player->charmRunePoison,
		player->charmRuneFreeze,
		player->charmRuneZap,
		player->charmRuneCurse,
		player->charmRuneCripple,
		player->charmRuneParry,
		player->charmRuneDodge,
		player->charmRuneAdrenaline,
		player->charmRuneNumb,
		player->charmRuneCleanse,
		player->charmRuneBless,
		player->charmRuneScavenge,
		player->charmRuneGut,
		player->charmRuneLowBlow,
		player->charmRuneDivine,
		player->charmRuneVamp,
		player->charmRuneVoid,
		player->UsedRunesBit,
		player->UnlockedRunesBit,
		escapedTrackerList,
		player->getGUID()
	);

	if (!g_database().executeQuery(updateQuery)) {
		g_logger().warn("[IOLoginDataSave::savePlayerBestiarySystem] - Error executing bestiary data update for player: {}", player->getName());
		return false;
	}

	return true;
}

bool IOLoginDataSave::savePlayerItem(const std::shared_ptr<Player> &player) {
	if (!player) {
		g_logger().warn("[IOLoginDataSave::savePlayerItem] - Player nullptr: {}", __FUNCTION__);
		return false;
	}

	if (!g_database().executeQuery(fmt::format("DELETE FROM `player_items` WHERE `player_id` = {}", player->getGUID()))) {
		g_logger().warn("[IOLoginDataSave::savePlayerItem] - Failed to delete items for player: {}", player->getName());
		return false;
	}

	DBInsert itemsQuery("INSERT INTO `player_items` (`player_id`, `pid`, `sid`, `itemtype`, `count`, `attributes`) VALUES ");
	PropWriteStream propWriteStream;

	ItemBlockList items;
	for (int32_t slotId = CONST_SLOT_FIRST; slotId <= CONST_SLOT_LAST; ++slotId) {
		const auto &item = player->inventory[slotId];
		if (item) {
			items.emplace_back(slotId, item);
		}
	}

	if (!saveItems(player, items, itemsQuery, propWriteStream)) {
		g_logger().warn("[IOLoginDataSave::savePlayerItem] - Failed to save items for player: {}", player->getName());
		return false;
	}

	return true;
}

bool IOLoginDataSave::savePlayerDepotItems(const std::shared_ptr<Player> &player) {
	if (!player) {
		g_logger().warn("[IOLoginDataSave::savePlayerDepotItems] - Player nullptr: {}", __FUNCTION__);
		return false;
	}
	PropWriteStream propWriteStream;

	if (!g_database().executeQuery(fmt::format("DELETE FROM `player_depotitems` WHERE `player_id` = {}", player->getGUID()))) {
		g_logger().warn("[IOLoginDataSave::savePlayerDepotItems] - Failed to delete depot items for player: {}", player->getName());
		return false;
	}

	DBInsert depotQuery("INSERT INTO `player_depotitems` (`player_id`, `pid`, `sid`, `itemtype`, `count`, `attributes`) VALUES ");

	ItemDepotList depotList;
	for (const auto &[pid, depotChest] : player->depotChests) {
		for (const std::shared_ptr<Item> &item : depotChest->getItemList()) {
			depotList.emplace_back(pid, item);
		}
	}

	if (!saveItems(player, depotList, depotQuery, propWriteStream)) {
		g_logger().warn("[IOLoginDataSave::savePlayerDepotItems] - Failed to save depot items for player: {}", player->getName());
		return false;
	}

	return true;
}

bool IOLoginDataSave::saveRewardItems(const std::shared_ptr<Player> &player) {
	if (!player) {
		g_logger().warn("[IOLoginDataSave::saveRewardItems] - Player nullptr: {}", __FUNCTION__);
		return false;
	}

	if (!g_database().executeQuery(fmt::format("DELETE FROM `player_rewards` WHERE `player_id` = {}", player->getGUID()))) {
		g_logger().warn("[IOLoginDataSave::saveRewardItems] - Failed to delete depot items for player: {}", player->getName());
		return false;
	}

	PropWriteStream propWriteStream;

	DBInsert rewardQuery("INSERT INTO `player_rewards` (`player_id`, `pid`, `sid`, `itemtype`, `count`, `attributes`) VALUES ");

	ItemRewardList rewardListItems;
	std::vector<uint64_t> rewardList;
	player->getRewardList(rewardList);

	if (!rewardList.empty()) {
		for (const auto &rewardId : rewardList) {
			const auto &reward = player->getReward(rewardId, false);
			if (reward && !reward->empty() && (getTimeMsNow() - rewardId <= 1000 * 60 * 60 * 24 * 7)) {
				rewardListItems.emplace_back(0, reward);
			}
		}
	}

	if (!saveItems(player, rewardListItems, rewardQuery, propWriteStream)) {
		g_logger().warn("[IOLoginDataSave::saveRewardItems] - Failed to save reward items for the player: {}", player->getName());
		return false;
	}

	return true;
}

bool IOLoginDataSave::savePlayerInbox(const std::shared_ptr<Player> &player) {
	if (!player) {
		g_logger().warn("[IOLoginDataSave::savePlayerInbox] - Player nullptr: {}", __FUNCTION__);
		return false;
	}
	if (!g_database().executeQuery(fmt::format("DELETE FROM `player_inboxitems` WHERE `player_id` = {}", player->getGUID()))) {
		g_logger().warn("[IOLoginDataSave::savePlayerInbox] - Failed to delete depot items for player: {}", player->getName());
		return false;
	}

	PropWriteStream propWriteStream;

	DBInsert inboxQuery("INSERT INTO `player_inboxitems` (`player_id`, `pid`, `sid`, `itemtype`, `count`, `attributes`) VALUES ");

	ItemInboxList inboxList;
	for (const auto &item : player->getInbox()->getItemList()) {
		inboxList.emplace_back(0, item);
	}

	if (!saveItems(player, inboxList, inboxQuery, propWriteStream)) {
		g_logger().warn("[IOLoginDataSave::savePlayerInbox] - Failed to save inbox items for player: {}", player->getName());
		return false;
	}

	return true;
}

bool IOLoginDataSave::savePlayerPreyClass(const std::shared_ptr<Player> &player) {
	if (!g_configManager().getBoolean(PREY_ENABLED)) {
		return true;
	}

	if (!player) {
		g_logger().warn("[IOLoginDataSave::savePlayerPreyClass] - Player nullptr: {}", __FUNCTION__);
		return false;
	}

	DBInsert preyQuery("INSERT INTO player_prey "
	                   "(`player_id`, `slot`, `state`, `raceid`, `option`, `bonus_type`, `bonus_rarity`, "
	                   "`bonus_percentage`, `bonus_time`, `free_reroll`, `monster_list`) VALUES ");
	preyQuery.upsert({ "state", "raceid", "option", "bonus_type", "bonus_rarity",
	                   "bonus_percentage", "bonus_time", "free_reroll", "monster_list" });

	auto playerGUID = player->getGUID();
	for (uint8_t slotId = PreySlot_First; slotId <= PreySlot_Last; slotId++) {
		if (const auto &slot = player->getPreySlotById(static_cast<PreySlot_t>(slotId))) {
			PropWriteStream propPreyStream;
			for (uint16_t raceId : slot->raceIdList) {
				propPreyStream.write<uint16_t>(raceId);
			}

			size_t preySize;
			const char* preyList = propPreyStream.getStream(preySize);
			auto escapedPreyList = g_database().escapeBlob(preyList, static_cast<uint32_t>(preySize));

			// Format row data for batch insert
			auto row = fmt::format("{}, {}, {}, {}, {}, {}, {}, {}, {}, {}, {}", playerGUID, static_cast<uint16_t>(slot->id), static_cast<uint16_t>(slot->state), slot->selectedRaceId, static_cast<uint16_t>(slot->option), static_cast<uint16_t>(slot->bonus), static_cast<uint16_t>(slot->bonusRarity), slot->bonusPercentage, slot->bonusTimeLeft, slot->freeRerollTimeStamp, escapedPreyList);

			if (!preyQuery.addRow(row)) {
				g_logger().warn("[IOLoginDataSave::savePlayerPreyClass] - Failed to add prey slot data for player: {}", player->getName());
				return false;
			}
		}
	}

	if (!preyQuery.execute()) {
		g_logger().warn("[IOLoginDataSave::savePlayerPreyClass] - Error executing prey slot data insertion for player: {}", player->getName());
		return false;
	}

	return true;
}

bool IOLoginDataSave::savePlayerTaskHuntingClass(const std::shared_ptr<Player> &player) {
	if (!g_configManager().getBoolean(TASK_HUNTING_ENABLED)) {
		return true;
	}

	if (!player) {
		g_logger().warn("[IOLoginDataSave::savePlayerTaskHuntingClass] - Player nullptr: {}", __FUNCTION__);
		return false;
	}
	DBInsert taskHuntQuery("INSERT INTO `player_taskhunt` "
	                       "(`player_id`, `slot`, `state`, `raceid`, `upgrade`, `rarity`, "
	                       "`kills`, `disabled_time`, `free_reroll`, `monster_list`) VALUES ");
	taskHuntQuery.upsert({ "state", "raceid", "upgrade", "rarity", "kills", "disabled_time",
	                       "free_reroll", "monster_list" });

	auto playerGUID = player->getGUID();
	for (uint8_t slotId = PreySlot_First; slotId <= PreySlot_Last; slotId++) {
		if (const auto &slot = player->getTaskHuntingSlotById(static_cast<PreySlot_t>(slotId))) {
			PropWriteStream propTaskHuntingStream;
			for (uint16_t raceId : slot->raceIdList) {
				propTaskHuntingStream.write<uint16_t>(raceId);
			}

			size_t taskHuntingSize;
			const char* taskHuntingList = propTaskHuntingStream.getStream(taskHuntingSize);
			auto escapedTaskHuntingList = g_database().escapeBlob(taskHuntingList, static_cast<uint32_t>(taskHuntingSize));

			// Construct row for batch insert
			auto row = fmt::format("{}, {}, {}, {}, {}, {}, {}, {}, {}, {}", playerGUID, static_cast<uint16_t>(slot->id), static_cast<uint16_t>(slot->state), slot->selectedRaceId, slot->upgrade ? 1 : 0, static_cast<uint16_t>(slot->rarity), slot->currentKills, slot->disabledUntilTimeStamp, slot->freeRerollTimeStamp, escapedTaskHuntingList);

			if (!taskHuntQuery.addRow(row)) {
				g_logger().warn("[IOLoginDataSave::savePlayerTaskHuntingClass] - Failed to add task hunting slot data for player: {}", player->getName());
				return false;
			}
		}
	}

	if (!taskHuntQuery.execute()) {
		g_logger().warn("[IOLoginDataSave::savePlayerTaskHuntingClass] - Error executing task hunting data insertion for player: {}", player->getName());
		return false;
	}

	return true;
}

bool IOLoginDataSave::savePlayerBosstiary(const std::shared_ptr<Player> &player) {
	if (!player) {
		g_logger().warn("[IOLoginDataSave::savePlayerBosstiary] - Player nullptr: {}", __FUNCTION__);
		return false;
	}

	// Use UPSERT to avoid the DELETE operation
	DBInsert insertQuery("INSERT INTO `player_bosstiary` "
	                     "(`player_id`, `bossIdSlotOne`, `bossIdSlotTwo`, `removeTimes`, `tracker`) VALUES ");
	insertQuery.upsert({ "bossIdSlotOne", "bossIdSlotTwo", "removeTimes", "tracker" });

	// Prepare tracker data using PropWriteStream
	PropWriteStream stream;
	for (const auto &monsterType : player->getCyclopediaMonsterTrackerSet(true)) {
		if (!monsterType) {
			continue;
		}
		stream.write<uint16_t>(monsterType->info.raceid);
	}

	size_t size;
	const char* trackerBlob = stream.getStream(size);
	auto escapedTrackerBlob = g_database().escapeBlob(trackerBlob, static_cast<uint32_t>(size));

	// Construct row for batch insert
	auto row = fmt::format("{}, {}, {}, {}, {}", player->getGUID(), player->getSlotBossId(1), player->getSlotBossId(2), player->getRemoveTimes(), escapedTrackerBlob);

	if (!insertQuery.addRow(row)) {
		g_logger().warn("[IOLoginDataSave::savePlayerBosstiary] - Failed to add bosstiary data for player: {}", player->getName());
		return false;
	}

	if (!insertQuery.execute()) {
		g_logger().warn("[IOLoginDataSave::savePlayerBosstiary] - Error executing bosstiary data insertion for player: {}", player->getName());
		return false;
	}

	return true;
}
