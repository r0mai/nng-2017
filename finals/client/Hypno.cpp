#include "Hypno.h"


Hypno::Hypno() {
	// nothing to see here
}

void Hypno::Process() {
}

CLIENT* CreateClient()
{
	return new Hypno();
}

std::vector<int> Hypno::GetOurHeroes() {
	std::vector<int> vec;
	for (auto& cc : mParser.Controllers) {
		if (cc.controller_id == 0) {
			vec.push_back(cc.hero_id);
		}
	}

	return vec;
}

std::vector<MAP_OBJECT> Hypno::GetOurTurrets() {
	std::vector<MAP_OBJECT> vec;
	for (auto& unit : mParser.Units) {
		if (unit.t == TURRET && unit.side == 0) {
			vec.push_back(unit);
		}
	}
	return vec;
}

std::vector<MAP_OBJECT> Hypno::GetEnemyTurrets() {
	std::vector<MAP_OBJECT> vec;
	for (auto& unit : mParser.Units) {
		if (unit.t == TURRET && unit.side == 1) {
			vec.push_back(unit);
		}
	}
	return vec;
}
