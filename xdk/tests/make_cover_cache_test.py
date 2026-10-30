"""Run the actual cover worker/mailbox and eviction code with desktop threads."""
from pathlib import Path
import sys
root=Path(__file__).resolve().parents[2]
source=(root/'src/app.c').read_text()
code=source[source.index('static volatile int art_cancel'):source.index('static void *applist_task')]
def function(signature):
    start=source.index(signature);end=source.index('{',start)+1;depth=1
    while depth:
        depth+=(source[end]=='{')-(source[end]=='}');end+=1
    return source[start:end]
code+='\n'+function('static void clear_apps(')+'\n'+function('static void stop_art_loader(')
fixture=(root/'xdk/tests/cover_cache_fixture.cpp').read_text()
Path(sys.argv[1]).write_text(fixture.replace('/* ACTUAL_COVER_WORKER */',code))
