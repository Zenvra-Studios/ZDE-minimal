# Command System

Commands are identified by stable textual IDs such as `zde.file.exit`. A
command contains presentation metadata, an optional shortcut, an execution
callback, and optional enabled/checked predicates.

`CommandRegistry` rejects duplicate IDs, prevents disabled commands from
executing, preserves registration order for future command-palette consumers,
and reports explicit execution results.

The native Win32 menu is only a View adapter: resource IDs are translated to
stable command IDs and forwarded to `StudioViewModel`. Product actions are not
implemented in the menu component.
