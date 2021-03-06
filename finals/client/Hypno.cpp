#include "Hypno.h"
#include <set>
#include <map>
#include <vector>
#include <iostream>
#include <algorithm>


CLIENT* CreateClient(std::string preferredOpponents) {
	return new Hypno(std::move(preferredOpponents));
}

Hypno::Hypno(std::string preferredOpponents) :
	mPreferredOpponents(std::move(preferredOpponents)) {
	// nothing to see here
}

Position Hypno::Retreat(const Matrix<double>& dmg_map, const MAP_OBJECT& hero) const {
	auto neighbours = GetNeighbours(hero.pos);
	auto target_pos = *std::min_element(begin(neighbours), end(neighbours),
		[&](auto lhs, auto rhs) {
			return dmg_map[lhs] < dmg_map[rhs];
		}
	);
	return target_pos;
}

void Hypno::MatchEnd() {
	mSuccesfulEnemyHeroes.clear();
	mLastPositionOfMinions.clear();
}

Position Hypno::FightOrFlight(int hero_id) const {
	auto hero = mParser.GetUnitByID(hero_id);
	if (IsNearOurBase(*hero)) {
		return hero->pos;
	}

	auto our_minion_map = GetDamageMap(GetOurMinions());
	auto enemy_minion_map = GetDamageMap(GetEnemyMinions());

	auto our_turret_map = GetDamageMap(GetOurTurrets());
	auto enemy_turret_map = GetDamageMap(GetEnemyTurrets());

	auto our_hero_map = GetDamageMap(GetOurHeroes());
	auto enemy_hero_map = GetDamageMap(GetEnemyHeroes());

	auto dmg_map =
		our_minion_map + enemy_minion_map +
		our_turret_map + enemy_turret_map +
		our_hero_map + enemy_hero_map;

	int minions_attacked = 0;
	int minions_in_range = 0;
	for (auto& minion : GetEnemyMinions()) {
		if (IsNeighbourOfCircle(hero->pos, minion.pos, MINION_RANGE_SQ)) {
		// if (hero->pos.DistSquare(minion.pos) <= MINION_RANGE_SQ) {
			if (our_turret_map[minion.pos] != 0 || our_minion_map[minion.pos] != 0) {
				++minions_attacked;
			}
			++minions_in_range;
		}
	}
	auto hp_map = GetHPMap();
	if (minions_in_range != minions_attacked) {
		// std::cerr << hero->pos << ": minions "
		// 	<< minions_attacked << "/" << minions_in_range << std::endl;
		if (!(hp_map[hero->pos] <= 10*MINION_MAX_HP)) {
			return Retreat(dmg_map, *hero);
		}
		// std::cerr << "Would retreat, but outnumber" << std::endl;
	}

	if (dmg_map[hero->pos] <= 0) {
		return hero->pos;
	}
	auto dmg_deficit = dmg_map[hero->pos];
	auto hp_surplus = -hp_map[hero->pos];
	if (hp_surplus > 2*dmg_deficit) {
		// If we can last two turns in this position, stay and fight
		return hero->pos;
	}
	return Retreat(dmg_map, *hero);
}

void Hypno::AttackMove(int hero_id, const Position& pos) {
	auto hero = mParser.GetUnitByID(hero_id);
	auto target_pos = FightOrFlight(hero_id);
	if (target_pos != hero->pos) {
		Move(hero_id, target_pos);
	} else {
		auto possible_targets = GetEnemyObjectsNear(hero->pos, HERO_RANGE_SQ);
		if (!possible_targets.empty()) {
			auto target_unit = GetPreferredEnemyToAttack(possible_targets);
			Attack(hero_id, target_unit);
			enemy_hp_map[target_unit] -= mParser.GetOurHeroDamage();
		} else if (hero->pos != pos) {
			Move(hero_id, mDistCache.GetNextTowards(hero->pos, pos));
		}
	}
}

void Hypno::AttackInside(const MAP_OBJECT& hero) {
	std::vector<MAP_OBJECT> enemies;
	for (auto& unit : GetEnemyHeroes()) {
		if (IsNearOurBase(unit)) {
			enemies.push_back(unit);
		}
	}

	std::sort(enemies.begin(), enemies.end(),
		[](const MAP_OBJECT& lhs, const MAP_OBJECT& rhs) {
			return lhs.hp < rhs.hp;
		});
	if (enemies.empty()) {
		AttackMid(hero);
	} else {
		AttackMove(hero.id, enemies.front().pos);
	}
}

void Hypno::AttackTop(const MAP_OBJECT& hero) {
	if (IsNearOurBase(hero)) {
		AttackMove(hero.id, {4, MaxY() - 4});
	} else {
		auto fallbacks = OrderByDst(GetTopFallbackObjects());
		if (fallbacks.size() < 2) {
			AttackMove(hero.id, {1, 11});
		} else {
			AttackMove(hero.id, fallbacks[0].pos);
		}

#if 0
		auto turrets = GetTopEnemyTurrets();
		if (turrets.empty()) {
			AttackMove(hero.id, {MaxX() - 1, MaxY() - 1});
		} else {
			auto target = turrets[0].pos;
			AttackMove(hero.id, target);
		}
#endif
	}
}

void Hypno::AttackDown(const MAP_OBJECT& hero) {
	if (IsNearOurBase(hero)) {
		AttackMove(hero.id, {MaxX() - 4, 4});
	} else {
		auto fallbacks = OrderByDst(GetDownFallbackObjects());
		if (fallbacks.size() < 2) {
			AttackMove(hero.id, {11, 1});
		} else {
			AttackMove(hero.id, fallbacks[0].pos);
		}
#if 0
		auto turrets = GetRightEnemyTurrets();
		if (turrets.empty()) {
			AttackMove(hero.id, {MaxX() - 1, MaxY() - 1});
		} else {
			auto target = turrets[0].pos;
			AttackMove(hero.id, target);
		}
#endif
	}
}

void Hypno::AttackMid(const MAP_OBJECT& hero) {
	auto turrets = GetMidEnemyTurrets();
	if (turrets.empty()) {
		AttackMove(hero.id, {MaxX() - 1, MaxY() - 1});
	} else {
		auto fallbacks = OrderByDst(GetMidFallbackObjects());
		if (fallbacks.size() < 2) {
			AttackMove(hero.id, {9, 9});
		} else {
			AttackMove(hero.id, fallbacks[0].pos);
		}
#if 0
		auto target = turrets[0].pos;
		AttackMove(hero.id, target);
#endif
	}
}

void Hypno::UpdateEnemyHeroes() {
	std::map<int, Position> newPositionOfMinions;
	for (const auto& minion: GetOurMinions()) {
		newPositionOfMinions.emplace(minion.id, minion.pos);
	}

	const auto& newHeroes = GetEnemyHeroes();
	for (auto& oldHero: mSuccesfulEnemyHeroes) {
		const auto& oldHeroId = oldHero.first;
		auto newIt = std::find_if(newHeroes.begin(), newHeroes.end(),
			[&oldHeroId](const MAP_OBJECT& o) {
				return o.id == oldHeroId;
			});
		if (newIt == newHeroes.end()) {
			oldHero.second = 0;
		}
	}

	for (const auto& minionRecord: mLastPositionOfMinions) {
		auto newIt = newPositionOfMinions.find(minionRecord.first);
		if (newIt != newPositionOfMinions.end()) {
			// Minion survived, nothing to do
			continue;
		}
		auto placeOfDeath = minionRecord.second;
		auto objectsNearPlaceOfDeath =
			GetEnemyObjectsNear(placeOfDeath, HERO_RANGE_SQ);
		for (const auto& object: objectsNearPlaceOfDeath) {
			if (object.t == UNIT_TYPE::HERO) {
				++mSuccesfulEnemyHeroes[object.id];
			}
		}
	}

	mLastPositionOfMinions = std::move(newPositionOfMinions);
}

std::map<int, int> Hypno::GetMostEvilEnemyHeroes() const {
	std::map<int, int> result;
	auto currentEnemyHeroes = GetEnemyHeroes();
	for (const auto& enemyHero: mSuccesfulEnemyHeroes) {
		auto heroId = enemyHero.first;
		auto it = std::find_if(currentEnemyHeroes.begin(), currentEnemyHeroes.end(),
				[heroId](const auto& o) { return o.id == heroId; });
		if (it != currentEnemyHeroes.end()) {
			result.emplace(enemyHero);
		}
	}
	return result;
}

void Hypno::Process() {
#if 0
	if (mParser.tick == 5) {
		for (auto& u : GetTopOurTurrets()) {
			std::cerr << "TURRET TOP " << u.id << " " << u.pos << std::endl;
		}
		for (auto& u : GetMidOurTurrets()) {
			std::cerr << "TURRET MID " << u.id << " " << u.pos << std::endl;
		}
		for (auto& u : GetDownOurTurrets()) {
			std::cerr << "TURRET DOWN " << u.id << " " << u.pos << std::endl;
		}
	}
#endif

	enemy_hp_map.clear();
	for (auto& enemy : GetEnemyObjects()) {
		enemy_hp_map[enemy.id] = enemy.hp;
	}
	UpdateEnemyHeroes();
#if 0
	for (const auto& enemyHero: GetMostEvilEnemyHeroes()) {
		std::cerr << "Hero " << enemyHero.first << " has been near: "
			<< enemyHero.second << " of our Minion's kills" << std::endl;
	}
#endif
	for (auto& hero : GetControlledHeroes()) {
		if (IsNearOurBase(hero)) {
			if (IsEnemyInside()) {
				AttackInside(hero);
			}
			if (!HasTopHero()) {
				AttackTop(hero);
			} else if (!HasDownHero()) {
				AttackDown(hero);
			} else {
				AttackMid(hero);
			}
		} else {
			if (IsAtTop(hero)) {
				AttackTop(hero);
			} else if (IsAtDown(hero)) {
				AttackDown(hero);
			} else {
				if (IsGangOfFourHigh(hero)) {
					if (!HasDownHero()) {
						AttackDown(hero);
					} else if (!HasTopHero()) {
						AttackTop(hero);
					} else {
						AttackMid(hero);
					}
				} else {
					AttackMid(hero);
				}
			}
		}
	}
}

std::vector<MAP_OBJECT> Hypno::GetControlledHeroes() const {
	std::vector<MAP_OBJECT> vec;
	std::set<int> ids;

	for (auto& cc : mParser.Controllers) {
		if (cc.controller_id == 0) {
			ids.insert(cc.hero_id);
		}
	}

	return GetObjects([&](const MAP_OBJECT& unit) {
		return (unit.t == HERO && ids.count(unit.id) > 0);
	});
}

std::vector<MAP_OBJECT> Hypno::GetHeroes(int side) const {
	return GetObjects([&](const MAP_OBJECT& unit) {
		return (unit.t == UNIT_TYPE::HERO && unit.side == side);
	});
}

std::vector<MAP_OBJECT> Hypno::GetOurHeroes() const {
	return GetHeroes(0);
}

std::vector<MAP_OBJECT> Hypno::GetEnemyHeroes() const {
	return GetHeroes(1);
}

std::vector<MAP_OBJECT> Hypno::GetMinions(int side) const {
	return GetObjects([&](const MAP_OBJECT& unit) {
		return (unit.t == MINION && unit.side == side);
	});
}

std::vector<MAP_OBJECT> Hypno::GetOurMinions() const {
	return GetMinions(0);
}

std::vector<MAP_OBJECT> Hypno::GetEnemyMinions() const {
	return GetMinions(1);
}

std::vector<MAP_OBJECT> Hypno::GetOurTurrets() const {
	return GetObjects([](const MAP_OBJECT& unit) {
		return unit.t == TURRET && unit.side == 0;
	});
}

std::vector<MAP_OBJECT> Hypno::GetEnemyTurrets() const {
	return GetObjects([](const MAP_OBJECT& unit) {
		return unit.t == TURRET && unit.side == 1;
	});
}

std::vector<MAP_OBJECT> Hypno::GetEnemyObjects() const {
	return GetObjects([](const MAP_OBJECT& obj) {
		return obj.side != 0;
	});
}

std::vector<MAP_OBJECT> Hypno::GetOurObjects() const {
	return GetObjects([](const MAP_OBJECT& obj) {
		return obj.side == 0;
	});
}

std::vector<MAP_OBJECT> Hypno::GetEnemyObjectsNear(
	const Position& pos, int distance_sq) const
{
	return GetObjects([&](const MAP_OBJECT& obj) {
		return obj.side != 0 && pos.DistSquare(obj.pos) <= distance_sq;
	});
}

MAP_OBJECT Hypno::GetEnemyBase() const {
	for (auto& unit : mParser.Units) {
		if (unit.t == UNIT_TYPE::BASE && unit.side != 0) {
			return unit;
		}
	}
	std::cerr << "No enemy base found :scream:" << std::endl;
	return {};
}

std::vector<MAP_OBJECT> Hypno::GetObjectsNear(
	const Position& pos, int distance_sq) const
{
	return GetObjects([&](const MAP_OBJECT& obj) {
		return pos.DistSquare(obj.pos) <= distance_sq;
	});
}

std::vector<Position> Hypno::GetNeighbours(const Position& pos) const {
	std::vector<Position> result;
	for (int y = pos.y - 1; y <= pos.y + 1; ++y) {
		for (int x = pos.x - 1; x <= pos.x + 1; ++x) {
			if (x == pos.x && y == pos.y) {
				continue;
			}
			if (x < 0 || x > MaxX() || y < 0 || y > MaxY()) {
				continue;
			}
			if (mParser.GetAt(Position{x, y}) == PARSER::GROUND_TYPE::WALL) {
				continue;
			}
			result.emplace_back(x, y);
		}
	}
	return result;
}

bool Hypno::IsNeighbourOfCircle(
	const Position& pos, const Position& center, int radius_sq) const
{
	auto d_sq = pos.DistSquare(center);
	if (d_sq <= radius_sq) {
		return true;
	}

	for (int y = pos.y - 1; y <= pos.y + 1; ++y) {
		for (int x = pos.x - 1; x <= pos.x + 1; ++x) {
			auto n_d_sq = Position{x, y}.DistSquare(center);
			if (n_d_sq <= radius_sq) {
				return true;
			}
		}
	}
	return false;
}

bool Hypno::CanOneHit(const MAP_OBJECT& unit) const {
	return enemy_hp_map.at(unit.id) <= mParser.GetHeroDamage(!unit.side);
}

int Hypno::GetPreferredEnemyToAttack(const std::vector<MAP_OBJECT>& enemies_) const {
	assert(!enemies_.empty());

	std::vector<MAP_OBJECT> enemies;
	for (auto& enemy : enemies_) {
		if (enemy_hp_map.at(enemy.id) > 0) {
			enemies.push_back(enemy);
		}
	}

	// everybody dead
	if (enemies.empty()) {
		return enemies_.front().id;
	}

	// one hit a turret if we can
	for (auto& enemy : enemies) {
		if (enemy.t == UNIT_TYPE::TURRET && CanOneHit(enemy)) {
			return enemy.id;
		}
	}

	// /*one hit*/ a hero if we can
	for (auto& enemy : enemies) {
		if (enemy.t == UNIT_TYPE::HERO /*&& CanOneHit(enemy)*/) {
			return enemy.id;
		}
	}

	// one hit a minion if we can
	for (auto& enemy : enemies) {
		if (enemy.t == UNIT_TYPE::MINION && CanOneHit(enemy)) {
			return enemy.id;
		}
	}

	auto target = enemies.front();
	for (auto& enemy : enemies) {
		if (enemy.hp < target.hp) {
			target = enemy;
		}
	}

	return target.id;
}

std::vector<MAP_OBJECT> Hypno::GetObjects(std::function<bool(const MAP_OBJECT&)> fn) const {
	std::vector<MAP_OBJECT> vec;
	for (auto& unit : mParser.Units) {
		if (fn(unit)) {
			vec.push_back(unit);
		}
	}

	return vec;
}

Matrix<double> Hypno::GetDamageMap() const {
	return GetDamageMap(mParser.Units);
}

Matrix<double> Hypno::GetDamageMap(const std::vector<MAP_OBJECT>& units) const {
	Matrix<double> result{
		static_cast<Matrix<double>::size_type>(mParser.w),
		static_cast<Matrix<double>::size_type>(mParser.h),
		0
	};


	for (auto& unit : units) {
		// skip bases for now
		if (unit.t == UNIT_TYPE::BASE) {
			continue;
		}

		int sign = (unit.side == 0 ? -1 : 1);

		int range_sq = mParser.GetAttackRangeSquaredOfUnit(unit);
		int dmg = mParser.GetDamageOfUnit(unit);

		const int radius = 6;
		for (int y = unit.pos.y - radius; y <= unit.pos.y + radius; ++y) {
			for (int x = unit.pos.x - radius; x <= unit.pos.x + radius; ++x) {
				if (x < 0 || x >= MaxX() || y < 0 || y >= MaxY()) {
					continue;
				}
				if (!IsNeighbourOfCircle(Position{x, y}, unit.pos, range_sq)) {
					continue;
				}
				result.at(x, y) += sign * dmg;
			}
		}
	}

	return result;
}

Matrix<double> Hypno::GetHPMap() const {
	Matrix<double> result{
		static_cast<Matrix<double>::size_type>(mParser.w),
		static_cast<Matrix<double>::size_type>(mParser.h),
		0
	};


	for (auto& unit : mParser.Units) {
		// skip bases for now
		if (unit.t == UNIT_TYPE::BASE) {
			continue;
		}

		int sign = (unit.side == 0 ? -1 : 1);

		int range_sq = mParser.GetAttackRangeSquaredOfUnit(unit);
		int hp = unit.hp;
		if (unit.t == UNIT_TYPE::HERO && unit.side == 0) {
			hp = mParser.GetMaxHPOfUnit(unit);
		}

		const int radius = 6;
		for (int y = unit.pos.y - radius; y <= unit.pos.y + radius; ++y) {
			for (int x = unit.pos.x - radius; x <= unit.pos.x + radius; ++x) {
				if (x < 0 || x > MaxX() || y < 0 || y > MaxY()) {
					continue;
				}
				if (!IsNeighbourOfCircle(Position{x, y}, unit.pos, range_sq)) {
					continue;
				}
				result(x, y) += sign * hp;
			}
		}
	}

	return result;
}

Matrix<double> Hypno::GetHeatMap() const {
	return GetTowerHeatMap() + GetUnitHeatMap();
}

Matrix<double> Hypno::GetTowerHeatMap() const {
	Matrix<double> result{
			static_cast<Matrix<double>::size_type>(mParser.w),
			static_cast<Matrix<double>::size_type>(mParser.h),
			0
	};

	static constexpr int friendly = 10;
	static constexpr int enemy = 20;

	static constexpr int tower = 40;

	for (const auto& enemyTurret: GetEnemyTurrets()) {
		for (const auto& cell: GetObjectsNear(enemyTurret.pos, TURRET_RANGE_SQ))
		{
			result[cell.pos] += enemy * tower;
		}
	}

	for (const auto& ourTurret: GetOurTurrets()) {
		for (const auto& cell: GetObjectsNear(ourTurret.pos, TURRET_RANGE_SQ))
		{
			result[cell.pos] -= friendly * tower;
		}
	}

	return result;
}

Matrix<double> Hypno::GetUnitHeatMap() const {
	Matrix<double> result{
			static_cast<Matrix<double>::size_type>(mParser.w),
			static_cast<Matrix<double>::size_type>(mParser.h),
			0
	};

	static constexpr int friendly = 10;
	static constexpr int enemy = 20;

	static constexpr int minion = 5;
	static constexpr int hero = 10; // TODO: Make this depend on level/hp
	static constexpr int effectWidth = 5;

	std::map<Position, int> sources;
	for (const auto& ourHero: GetOurHeroes()) {
		result[ourHero.pos] -= friendly * hero;
		sources[ourHero.pos] = result[ourHero.pos];
	}

	for (const auto& enemyHero: GetEnemyHeroes()) {
		result[enemyHero.pos] += enemy * hero;
		sources[enemyHero.pos] = result[enemyHero.pos];
	}

	for (const auto& ourMinion: GetOurMinions()) {
		result[ourMinion.pos] -= friendly * minion;
		sources[ourMinion.pos] = result[ourMinion.pos];
	}

	for (const auto& enemyMinion: GetEnemyMinions()) {
		result[enemyMinion.pos] += enemy * minion;
		sources[enemyMinion.pos] = result[enemyMinion.pos];
	}

	for (const auto& source: sources) {
		const auto& sourceX = source.first.x;
		const auto& sourceY = source.first.y;
		for (int x = sourceX-effectWidth; x < sourceX+effectWidth + 1; ++x) {
			for (int y = sourceY-effectWidth; y < sourceY+effectWidth + 1; ++y)
			{
				if (x < 0 || x > MaxX() || y < 0 || y > MaxY()) {
					continue;
				}
				auto currentPosition = Position{x, y};
				auto distance = std::abs(
						mDistCache.GetDist(currentPosition, source.first));
				result[currentPosition] +=
						(double(effectWidth - distance)/effectWidth) *
						double(source.second);
			}
		}
	}

	return result;
}

bool Hypno::IsTopLane(const Position& pos) const {
	return pos.y > MaxY() - 4;
}

bool Hypno::IsDownLane(const Position& pos) const {
	return pos.y < 4;
}

bool Hypno::IsLeftLane(const Position& pos) const {
	return pos.x < 4;
}

bool Hypno::IsRightLane(const Position& pos) const {
	return pos.x > MaxX() - 4;
}

std::vector<MAP_OBJECT> Hypno::GetTopEnemyTurrets() const {
	return OrderByX(GetObjects([&](const MAP_OBJECT& unit) {
		return unit.t == TURRET && unit.side == 1 && IsAtTop(unit);
	}));
}

std::vector<MAP_OBJECT> Hypno::GetDownEnemyTurrets() const {
	return OrderByY(GetObjects([&](const MAP_OBJECT& unit) {
		return unit.t == TURRET && unit.side == 1 && IsAtTop(unit);
	}));
}

std::vector<MAP_OBJECT> Hypno::GetMidEnemyTurrets() const {
	return OrderByX(GetObjects([&](const MAP_OBJECT& unit) {
		auto lane = std::abs(GetLane(unit.pos));
		return unit.t == TURRET && unit.side == 1 && lane < 4;
	}));
}

std::vector<MAP_OBJECT> Hypno::GetTopOurTurrets() const {
	return OrderByX(GetObjects([&](const MAP_OBJECT& unit) {
		return unit.t == TURRET && unit.side == 0 && IsAtTop(unit);
	}));
}

std::vector<MAP_OBJECT> Hypno::GetDownOurTurrets() const {
	return OrderByY(GetObjects([&](const MAP_OBJECT& unit) {
		return unit.t == TURRET && unit.side == 0 && IsAtDown(unit);
	}));
}

std::vector<MAP_OBJECT> Hypno::GetMidOurTurrets() const {
	return OrderByX(GetObjects([&](const MAP_OBJECT& unit) {
		auto lane = std::abs(GetLane(unit.pos));
		return unit.t == TURRET && unit.side == 0 && lane < 4;
	}));
}

std::vector<MAP_OBJECT> Hypno::GetTopFallbackObjects() const {
	auto minions = GetTopMinions();
	auto turrets = GetTopOurTurrets();
	minions.insert(minions.end(), turrets.begin(), turrets.end());
	return minions;
}

std::vector<MAP_OBJECT> Hypno::GetDownFallbackObjects() const {
	auto minions = GetDownMinions();
	auto turrets = GetDownOurTurrets();
	minions.insert(minions.end(), turrets.begin(), turrets.end());
	return minions;
}

std::vector<MAP_OBJECT> Hypno::GetMidFallbackObjects() const {
	auto minions = GetMidMinions();
	auto turrets = GetMidOurTurrets();
	minions.insert(minions.end(), turrets.begin(), turrets.end());
	return minions;
}

std::vector<MAP_OBJECT> Hypno::OrderByX(
	std::vector<MAP_OBJECT> units, bool reverse) const
{
	std::sort(units.begin(), units.end(), LessByX);
	if (reverse) {
		std::reverse(units.begin(), units.end());
	}
	return units;
}

std::vector<MAP_OBJECT> Hypno::OrderByDst(
	std::vector<MAP_OBJECT> units, bool reverse) const
{
	std::sort(units.begin(), units.end(), LessByDst());
	if (reverse) {
		std::reverse(units.begin(), units.end());
	}
	return units;
}

std::vector<MAP_OBJECT> Hypno::OrderByY(
	 std::vector<MAP_OBJECT> units, bool reverse) const
{
	std::sort(units.begin(), units.end(), LessByY);
	if (reverse) {
		std::reverse(units.begin(), units.end());
	}
	return units;
}

bool Hypno::LessByX(const MAP_OBJECT& lhs, const MAP_OBJECT& rhs) {
	return lhs.pos.x < rhs.pos.x;
}

bool Hypno::LessByY(const MAP_OBJECT& lhs, const MAP_OBJECT& rhs) {
	return lhs.pos.y < rhs.pos.y;
}

bool Hypno::LessByHp(const MAP_OBJECT& lhs, const MAP_OBJECT& rhs) {
	return lhs.hp < rhs.hp;
}

Hypno::ObjectCompare Hypno::LessByDst() const {
	return [&](const MAP_OBJECT& lhs, const MAP_OBJECT& rhs) {
		auto target = GetEnemyBase().pos;
		return
			mDistCache.GetDist(lhs.pos, target) <
			mDistCache.GetDist(rhs.pos, target);
	};
}

int Hypno::MaxX() const {
	return mParser.w - 1;
}

int Hypno::MaxY() const {
	return mParser.h - 1;
}

int Hypno::GetLane(const Position& pos) const {
	return pos.y - pos.x;
}

int Hypno::GetAdvance(const Position& pos) const {
	return pos.x + pos.y;
}

int Hypno::PreferLane(const MAP_OBJECT& hero) const {
	const int advance_max = MaxX() + MaxY();
	const int lane_sep = 6;

	const int advance_low = 30;
	const int advance_high = advance_max - advance_low;

	const int mod_high = -2;
	const int mod_mid_high = -6;

	const int mod_low = 4;
	const int mod_mid_low = 3;

	const int mod_enemy_low = 6;
	const int mod_enemy_high = 3;
	const int mod_enemy_mid_low = 4;
	const int mod_enemy_mid_high = 2;

	int top = 0;
	int mid = 2;
	int down = 0;

	for (auto& unit : GetOurHeroes()) {
		if (unit.id == hero.id) {
			continue;
		}

		auto lane = GetLane(unit.pos);
		auto advance = GetAdvance(unit.pos);

		if (lane > lane_sep) {
			if (advance > advance_high) {
				top += mod_high;
			} else if (advance > advance_low) {
				top += mod_low;
			}
		} else if (lane < -lane_sep) {
			if (advance > advance_high) {
				down += mod_high;
			} else if (advance > advance_low) {
				down += mod_low;
			}
		} else {
			if (advance > advance_high) {
				mid += mod_mid_high;
			} else if (advance > advance_low) {
				mid += mod_mid_low;
			}
		}
	}

	for (auto& unit : GetEnemyHeroes()) {
		auto lane = GetLane(unit.pos);
		auto advance = GetAdvance(unit.pos);

		if (lane > lane_sep) {
			if (advance < advance_low) {
				top += mod_enemy_low;
			} else if (advance < advance_high) {
				top += mod_enemy_high;
			}
		} else if (lane < -lane_sep) {
			if (advance < advance_low) {
				down += mod_enemy_low;
			} else if (advance < advance_high) {
				down += mod_enemy_high;
			}
		} else {
			if (advance < advance_low) {
				mid += mod_enemy_mid_low;
			} else if (advance < advance_high) {
				mid += mod_enemy_mid_high;
			}
		}
	}

	if (mid >= top && mid >= down) {
		return 0;
	} else {
		return top > down ? 1 : -1;
	}
}

bool Hypno::IsNearOurBase(const MAP_OBJECT& unit, int dst) const {
	return unit.pos.x < dst && unit.pos.y < dst;
}

bool Hypno::IsAtTop(const MAP_OBJECT& unit) const {
	auto pos = unit.pos;
	return
		(pos.y > 12 && pos.x < 8) ||
		(pos.y > MaxY() - 8 && pos.x < MaxX() - 12) ||
		((pos.y > MaxY() - 8 && pos.x >= MaxX() - 12) && GetLane(unit.pos) > 5);
}

bool Hypno::IsAtDown(const MAP_OBJECT& unit) const {
	auto pos = unit.pos;
	return
		(pos.x > 12 && pos.y < 8) ||
		(pos.x > MaxX() - 8 && pos.y < MaxY() - 12) ||
		(pos.x > MaxX() - 8 && pos.y >= MaxY() - 12 && GetLane(unit.pos) < -5);
}

bool Hypno::IsAtMid(const MAP_OBJECT& unit) const {
	auto pos = unit.pos;
	return
		(pos.x > 12 || pos.y > 12) &&
		!IsAtTop(unit) && !IsAtDown(unit);
}


bool Hypno::HasTopHero() const {
	for (auto& unit : GetOurHeroes()) {
		if (IsAtTop(unit)) {
			return true;
		}
	}
	return false;
}

bool Hypno::HasDownHero() const {
	for (auto& unit : GetOurHeroes()) {
		if (IsAtDown(unit)) {
			return true;
		}
	}
	return false;
}

bool Hypno::IsEnemyInside() const {
	for (auto& unit : GetEnemyHeroes()) {
		if (IsNearOurBase(unit)) {
			return true;
		}
	}
	return false;
}

std::vector<MAP_OBJECT> Hypno::GetTopMinions() const {
	std::vector<MAP_OBJECT> vec;
	for (auto& minion : GetOurMinions()) {
		if (IsAtTop(minion)) {
			vec.push_back(minion);
		}
	}
	return vec;
}


std::vector<MAP_OBJECT> Hypno::GetDownMinions() const {
	std::vector<MAP_OBJECT> vec;
	for (auto& minion : GetOurMinions()) {
		if (IsAtDown(minion)) {
			vec.push_back(minion);
		}
	}
	return vec;
}

std::vector<MAP_OBJECT> Hypno::GetMidMinions() const {
	std::vector<MAP_OBJECT> vec;
	for (auto& minion : GetOurMinions()) {
		if (IsAtMid(minion)) {
			vec.push_back(minion);
		}
	}
	return vec;
}

bool Hypno::IsGangOfFourHigh(const MAP_OBJECT& unit) const {
	// if GOF in middle, and has highest id
	if (!IsAtMid(unit)) {
		return false;
	}

	std::vector<MAP_OBJECT> gof;
	for (auto& hero : GetOurHeroes()) {
		if (IsAtMid(hero)) {
			gof.push_back(hero);
		}
	}

	if (gof.empty() || gof.size() < 4) {
		// failsafe
		return false;
	}

	int high_id = gof[0].id;
	for (auto& hero : gof) {
		high_id = std::max(high_id, hero.id);
	}

	return unit.id == high_id;
}
