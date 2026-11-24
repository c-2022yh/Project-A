"""언리얼 Python에서 실행한다. 현재 저장된 라운드 설정을 통합 DT로 복사한다."""
import csv
import io
import unreal


def column(table, name):
    if not table:
        return {}
    fields = unreal.DataTableFunctionLibrary.get_data_table_column_names(table)
    field = next((field for field in fields if str(field) == name or str(field).startswith(name + "_")), None)
    if field is None:
        raise RuntimeError("Missing column " + name)
    names = unreal.DataTableFunctionLibrary.get_data_table_row_names(table)
    values = unreal.DataTableFunctionLibrary.get_data_table_column_as_string(table, field)
    return {str(key): value for key, value in zip(names, values)}


def create_unified_rounds():
    target = "/Game/Data/DT_RoundsUnified"
    if unreal.EditorAssetLibrary.does_asset_exist(target):
        raise RuntimeError("DT_RoundsUnified already exists. Existing data was preserved.")

    mode_class = unreal.load_class(
        None, "/Game/BluePrints/GameMode/BP_MainGameMode.BP_MainGameMode_C")
    mode = unreal.get_default_object(mode_class)
    flow = unreal.load_asset("/Game/Data/DT_Rounds")
    limits = column(flow, "MaxDeployedUnits")
    durations = column(flow, "CombatDurationSeconds")
    enemies = column(mode.get_editor_property("round_enemy_formations_table"), "Enemies")
    gold = column(mode.get_editor_property("round_rewards_table"), "ClearGold")
    unit_costs = column(mode.get_editor_property("main_map_unit_definitions"), "Cost")
    shop_class = unreal.load_class(
        None, "/Game/BluePrints/Shop/BP_ShopManager.BP_ShopManager_C")
    shop = unreal.get_default_object(shop_class)
    pool = [
        {
            "Kind": "Gold" if offer.kind == unreal.RewardKind.GOLD else "Unit" if offer.kind == unreal.RewardKind.UNIT else "Item",
            "DefinitionId": str(offer.definition_id),
            "Stars": offer.stars,
            "Weight": offer.weight,
            "GoldAmount": offer.gold_amount,
        }
        for offer in shop.get_editor_property("reward_pool")
        if offer.weight > 0
    ]
    if not pool:
        # 기존 메인맵의 자동 보상 후보와 같이 1코스트 기물부터 채운다.
        candidates = [
            (name, int(cost)) for name, cost in unit_costs.items()
            if 1 <= int(cost) <= 5
        ]
        candidates.sort(key=lambda entry: (entry[1] != 1, entry[0]))
        pool = [
            {"Kind": "Unit", "DefinitionId": name, "Stars": 1, "Weight": 1}
            for name, cost in candidates
            if cost == 1
        ]
        for name, cost in candidates:
            if len(pool) >= 4:
                break
            if cost != 1:
                pool.append({"Kind": "Unit", "DefinitionId": name, "Stars": 1, "Weight": 1})
    if len(pool) < 4:
        raise RuntimeError("At least four valid reward candidates are required.")

    def quoted(value):
        return '"' + value.replace("\\", "\\\\").replace('"', '\\"') + '"'

    pool_text = "(" + ",".join(
        "(Kind={Kind},DefinitionId={id},Stars={Stars},Weight={Weight},GoldAmount={amount})".format(
            id=quoted(entry["DefinitionId"]), amount=entry.get("GoldAmount", 0), **entry) for entry in pool
    ) + ")"
    output = io.StringIO()
    writer = csv.writer(output, lineterminator="\n")
    writer.writerow(["Name", "MaxDeployedUnits", "CombatDurationSeconds", "Enemies", "ClearGold", "RewardPool"])
    for number in range(1, 6):
        name = f"R{number}"
        if name not in limits or name not in durations or name not in enemies or enemies[name] in ("", "()"):
            raise RuntimeError(f"Missing saved round/enemy formation for {name}. Save the tables first.")
        writer.writerow([name, limits[name], durations[name], enemies[name], gold.get(name, "0"), pool_text])

    factory = unreal.DataTableFactory()
    factory.set_editor_property("struct", unreal.load_object(
        None, "/Script/Project_A.AutoBattleRoundDefinition"))
    table = unreal.AssetToolsHelpers.get_asset_tools().create_asset(
        "DT_RoundsUnified", "/Game/Data", unreal.DataTable, factory)
    if not table or not unreal.DataTableFunctionLibrary.fill_data_table_from_csv_string(
            table, output.getvalue()):
        raise RuntimeError("Could not create the unified round data table.")
    if not unreal.EditorAssetLibrary.save_loaded_asset(table):
        raise RuntimeError("Could not save the unified round data table.")
    unreal.log("UNIFIED_ROUNDS_CREATED " + target
               + " — assign it to BP_MainGameMode > Rounds Table.")
    return table


if __name__ == "__main__":
    create_unified_rounds()
