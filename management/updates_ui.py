from typing import Callable
# used with ui.timer, define callbacks for ui updates
# id: nicegui context id, list: list of callbacks\
timer_cbs = dict[str, list[Callable]]()

class Updates:
    @staticmethod
    def REGISTER_TIMER_CALLBACK(id: str, cb: Callable):
        if id not in timer_cbs:
            timer_cbs[id] = []

        # overwrite old callback, noticed some problems if functions are weirdly defined
        # inside other funcs (see _update_state), as it would call "older version" of the function
        # probably cause this code is absolute trash :/
        for i, ex in enumerate(timer_cbs[id]):
            if ex.__name__ == cb.__name__:
                del timer_cbs[id][i]
                break

        timer_cbs[id].append(cb)

    @staticmethod
    def UNREGISTER_ID_CALLBACK(id: str, cb: Callable):
        if id in timer_cbs:
            del timer_cbs[id]
    
    def run(self):
        for cb_list in timer_cbs.values():
            for cb in cb_list:
                cb()