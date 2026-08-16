from pathlib import Path
for rel in [
    '.github/cleanup_note.tmp',
    '.github/time_ruler_patch.py',
    '.github/time_ruler_optional_patch.py',
    '.github/workflows/apply-time-ruler.yml',
    '.github/workflows/apply-time-ruler-optional.yml',
]:
    p = Path(rel)
    if p.exists():
        p.unlink()
print('time ruler patch helpers removed')
