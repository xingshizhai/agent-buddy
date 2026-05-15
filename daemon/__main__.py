from daemon.main import main
import asyncio
try:
    asyncio.run(main())
except KeyboardInterrupt:
    pass
