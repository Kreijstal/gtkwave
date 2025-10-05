#!/usr/bin/env python3

import pyatspi
import time


def find_gtkwave_app():
    """Find the GTKWave application."""
    desktop = pyatspi.Registry.getDesktop(0)
    for i in range(desktop.childCount):
        app = desktop.getChildAtIndex(i)
        if "gtkwave" in (getattr(app, "name", "") or "").lower():
            return app
    return None


def find_mysim_cell(acc, depth=0, max_depth=20):
    """Recursively find the 'mysim' table cell."""
    if depth > max_depth:
        return None

    try:
        role = acc.getRoleName() if hasattr(acc, "getRoleName") else ""
        name = getattr(acc, "name", "") or ""

        if "table cell" in role.lower() and "mysim" in name.lower():
            return acc

        for i in range(acc.childCount):
            child = acc.getChildAtIndex(i)
            result = find_mysim_cell(child, depth + 1, max_depth)
            if result:
                return result
    except Exception:
        pass

    return None


def find_sine_wave_cell(acc, depth=0, max_depth=50):
    """Recursively find the 'sine_wave' table cell."""
    if depth > max_depth:
        return None

    try:
        role = acc.getRoleName() if hasattr(acc, "getRoleName") else ""
        name = getattr(acc, "name", "") or ""

        if "table cell" in role.lower() and "sine_wave" in name.lower():
            print(f"Found sine_wave cell: '{name}', role: {role}")
            return acc

        for i in range(acc.childCount):
            child = acc.getChildAtIndex(i)
            result = find_sine_wave_cell(child, depth + 1, max_depth)
            if result:
                return result
    except Exception:
        pass

    return None


def find_menu_item(acc, menu_name, item_name, depth=0, max_depth=20):
    """Recursively find a specific menu item."""
    if depth > max_depth:
        return None

    try:
        role = acc.getRoleName() if hasattr(acc, "getRoleName") else ""
        name = getattr(acc, "name", "") or ""

        # Look for menu items with the target name
        if "menu item" in role.lower() and item_name.lower() in name.lower():
            # Check if it's in the right menu by looking at parent
            parent = acc.parent
            if parent:
                parent_name = getattr(parent, "name", "") or ""
                if menu_name.lower() in parent_name.lower():
                    print(f"Found menu item: '{name}' in menu '{parent_name}'")
                    return acc

        # Recursively search children
        for i in range(acc.childCount):
            child = acc.getChildAtIndex(i)
            result = find_menu_item(child, menu_name, item_name, depth + 1, max_depth)
            if result:
                return result
    except Exception as e:
        pass

    return None


def focus_window(app):
    """Focus and raise the GTKWave window using wmctrl."""
    try:
        import subprocess

        for i in range(app.childCount):
            child = app.getChildAtIndex(i)
            if child.getRoleName() == "frame":
                window_name = child.name
                print(f"Window title: {window_name}")

                result = subprocess.run(
                    ["wmctrl", "-a", window_name], capture_output=True, text=True
                )

                if result.returncode == 0:
                    print("Raised GTKWave window to front")
                    time.sleep(0.3)
                    return True
                else:
                    result = subprocess.run(
                        ["xdotool", "search", "--name", window_name, "windowactivate"],
                        capture_output=True,
                        text=True,
                    )
                    if result.returncode == 0:
                        print("Raised window using xdotool")
                        time.sleep(0.3)
                        return True

    except Exception as e:
        print(f"Could not focus window: {e}")

    print("WARNING: Could not raise window, click may fail!")
    return False


def click_mysim(cell, app):
    """Click the mysim cell to expand hierarchy."""
    try:
        focus_window(app)

        component = cell.queryComponent()
        extents = component.getExtents(pyatspi.DESKTOP_COORDS)

        x = extents.x + extents.width // 2
        y = extents.y + extents.height // 2

        print(f"Clicking mysim at: ({x}, {y})")
        pyatspi.Registry.generateMouseEvent(x, y, "b1c")

        return True

    except Exception as e:
        print(f"Error clicking mysim: {e}")
        import traceback

        traceback.print_exc()
        return False


def double_click_sine_wave(cell, app):
    """Double-click the sine_wave cell."""
    try:
        focus_window(app)

        component = cell.queryComponent()
        extents = component.getExtents(pyatspi.DESKTOP_COORDS)

        x = extents.x + extents.width // 2
        y = extents.y + extents.height // 2

        print(f"Double-clicking sine_wave at: ({x}, {y})")

        # Generate double click: press-release, then press-release quickly
        pyatspi.Registry.generateMouseEvent(x, y, "b1c")
        time.sleep(0.1)  # Short delay between clicks
        pyatspi.Registry.generateMouseEvent(x, y, "b1c")

        return True

    except Exception as e:
        print(f"Error double-clicking sine_wave: {e}")
        import traceback

        traceback.print_exc()
        return False


def click_menu_item(menu_item, app):
    """Click a menu item using its action."""
    try:
        focus_window(app)
        time.sleep(0.5)  # Wait for menu to be ready

        # Use the action interface to click the menu item
        action = menu_item.queryAction()
        if action and action.nActions > 0:
            print(f"Clicking menu item using action 0")
            success = action.doAction(0)
            if success:
                print("Successfully clicked menu item")
                return True
            else:
                print("Action failed, trying mouse click")

        # Fallback to mouse click
        component = menu_item.queryComponent()
        extents = component.getExtents(pyatspi.DESKTOP_COORDS)

        x = extents.x + extents.width // 2
        y = extents.y + extents.height // 2

        print(f"Clicking menu item at: ({x}, {y})")
        pyatspi.Registry.generateMouseEvent(x, y, "b1c")

        return True

    except Exception as e:
        print(f"Error clicking menu item: {e}")
        import traceback

        traceback.print_exc()
        return False


def wait_for_sine_wave(app, timeout=10):
    """Wait for sine_wave to appear after expanding mysim."""
    print("Waiting for sine_wave to appear...")
    start_time = time.time()

    while time.time() - start_time < timeout:
        sine_wave_cell = find_sine_wave_cell(app)
        if sine_wave_cell:
            return sine_wave_cell
        time.sleep(0.5)

    return None


def main():
    timeout = 30
    start_time = time.time()

    print("Waiting for GTKWave application...")

    # Initial delay to allow GTKWave to start up and become visible to AT-SPI
    initial_delay = 5
    print(f"Waiting {initial_delay} seconds for GTKWave to start up...")
    time.sleep(initial_delay)

    while time.time() - start_time < timeout:
        app = find_gtkwave_app()

        if app:
            print("Found GTKWave application")

            # Step 1: Find and click mysim to expand hierarchy
            mysim_cell = find_mysim_cell(app)

            if mysim_cell:
                print(f"Found 'mysim' cell: {mysim_cell.name}")

                if click_mysim(mysim_cell, app):
                    print("Success! Clicked 'mysim' to expand hierarchy")

                    # Step 2: Wait for sine_wave to appear
                    time.sleep(1)  # Brief pause for hierarchy to expand
                    sine_wave_cell = wait_for_sine_wave(app)

                    if sine_wave_cell:
                        print("Found 'sine_wave' after expanding mysim")

                        # Step 3: Double-click sine_wave
                        if double_click_sine_wave(sine_wave_cell, app):
                            print("Success! Double-clicked 'sine_wave'")
                            time.sleep(1)  # Wait for signal to be added

                            # Step 4: Find and click "Highlight All" in Edit menu
                            highlight_all_item = find_menu_item(
                                app, "Edit", "Highlight All"
                            )
                            if highlight_all_item:
                                print("Found 'Highlight All' menu item")
                                if click_menu_item(highlight_all_item, app):
                                    print("Success! Clicked 'Highlight All'")
                                    time.sleep(0.5)

                                    # Step 5: Find and click "Toggle Group Open|Close" in Edit menu
                                    toggle_group_item = find_menu_item(
                                        app, "Edit", "Toggle Group Open|Close"
                                    )
                                    if toggle_group_item:
                                        print(
                                            "Found 'Toggle Group Open|Close' menu item"
                                        )
                                        if click_menu_item(toggle_group_item, app):
                                            print(
                                                "Success! Clicked 'Toggle Group Open|Close'"
                                            )
                                            return 0
                                        else:
                                            print(
                                                "Failed to click 'Toggle Group Open|Close'"
                                            )
                                            return 1
                                    else:
                                        print(
                                            "'Toggle Group Open|Close' menu item not found"
                                        )
                                        return 1
                                else:
                                    print("Failed to click 'Highlight All'")
                                    return 1
                            else:
                                print("'Highlight All' menu item not found")
                                return 1
                        else:
                            print("Failed to double-click 'sine_wave'")
                            return 1
                    else:
                        print("'sine_wave' not found after expanding mysim")
                        return 1
                else:
                    print("Could not click 'mysim'")
                    return 1
            else:
                print("'mysim' not found yet, waiting...")
        else:
            print("GTKWave not found yet, waiting...")

        time.sleep(0.5)

    print(f"Timeout: GTKWave or required elements not found within {timeout} seconds")
    return 1


if __name__ == "__main__":
    exit(main())
