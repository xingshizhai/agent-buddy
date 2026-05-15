# daemon/services/base.py
from abc import ABC, abstractmethod


class ServiceBase(ABC):
    """Base class for all service pollers.

    Subclasses set ``service_id`` and ``poll_interval`` as class attributes
    and implement ``poll()``.
    """

    service_id: str    # e.g. "claude", "cursor"
    poll_interval: int  # seconds between polls

    @abstractmethod
    async def poll(self) -> dict | None:
        """Poll the upstream service.

        Returns a payload dict suitable for ``protocol.make_data()``,
        or ``None`` if the poll failed (daemon will retry next interval).
        """
