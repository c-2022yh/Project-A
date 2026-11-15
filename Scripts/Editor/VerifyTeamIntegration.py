"""팀 브랜치 병합 후 보드 기본값을 연결하고 핵심 에셋 로딩 결과를 기록한다."""
import json
from pathlib import Path
import unreal

board_path = '/Game/BluePrints/Board/BP_BoardManager'
board_bp = unreal.load_asset(board_path)
assert board_bp, board_path
board_class = unreal.EditorAssetLibrary.load_blueprint_class(board_path)
bench_class = unreal.EditorAssetLibrary.load_blueprint_class('/Game/BluePrints/Board/BP_BenchTile')
assert board_class and bench_class
defaults = unreal.get_default_object(board_class)
# 기존 보드의 지형/메시 설정을 보존하고 새 대기석 BP만 연결한다.
if defaults.get_editor_property('bench_tile_class') != bench_class:
    defaults.set_editor_property('bench_tile_class', bench_class)
    unreal.BlueprintEditorLibrary.compile_blueprint(board_bp)
    assert unreal.EditorAssetLibrary.save_loaded_asset(board_bp, only_if_is_dirty=False)

report = {'board': board_path, 'bench_class': bench_class.get_path_name(), 'assets': []}
for root in ['/Game/BluePrints', '/Game/Data/Units', '/Game/Main', '/Game/TestMap', '/Game/ControlerTestMap', '/Game/ShopTests']:
    for path in unreal.EditorAssetLibrary.list_assets(root, recursive=True, include_folder=False):
        asset = unreal.load_asset(path)
        assert asset, 'Asset failed to load: ' + path
        report['assets'].append({'path': path, 'class': asset.get_class().get_name()})
table = unreal.load_asset('/Game/Data/Units/DT_Units')
report['unit_rows'] = [str(name) for name in unreal.DataTableFunctionLibrary.get_data_table_row_names(table)]
output = Path(unreal.Paths.project_saved_dir()) / 'TeamIntegrationAssetReport.json'
output.write_text(json.dumps(report, ensure_ascii=False, indent=2), encoding='utf-8')
unreal.log('TEAM_INTEGRATION_ASSETS_OK ' + str(output))
