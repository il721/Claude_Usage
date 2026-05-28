### DESIGN.md

### Overview
EasyBack follows a **Modern Dark** design system. The UI is designed to be high-contrast, professional, and intuitive, utilizing the "Lexend Light" font for a modern look. It separates layout (XML `.ui` files), styling (QSS in `all_styles.py`), and logic (Python dialog classes).

---

### Visual Style

#### 1. Color Palette
| Element | Color (HEX/RGB) | Usage |
| :--- | :--- | :--- |
| **Background** | `rgb(30, 30, 30)` | Main window and dialog backgrounds |
| **Primary Accent** | `#2B79C2` (Blue) | Borders, hover states, selected tabs, and labels |
| **Text (Primary)** | `rgb(230, 230, 230)` | Headlines and button text |
| **Text (Secondary)**| `rgb(150, 150, 150)` | Unselected tabs and disabled elements |
| **Button (Normal)** | `rgba(60, 60, 60, 80)` | Standard button background |
| **Button (Hover)** | `rgba(30, 30, 30, 180)` | Interactive feedback |

#### 2. Typography
- **Primary Font**: `Lexend Light`
- **Sizes**:
    - **Large Buttons/Headers**: `25pt`
    - **Dialog Text**: `19pt` (300 weight)
    - **Settings/Small Buttons**: `16pt`
    - **Labels**: `12pt`

#### 3. Component Styling (QSS)
All components use **rounded corners** (`border-radius: 15px` to `20px`) and **thick borders** (`2px` to `3px`).
- **Interactive States**: Buttons change border color and background transparency on `:hover` and invert colors on `:pressed`.

---

### UI Architecture

#### 1. Layout Management
- **Qt Designer**: Layouts are initially designed in `.ui` files (found in `ui/`).
- **Compilation**: The `ui2py.py` script converts `.ui` files into Python classes using `pyside6-uic`.
- **Scaling**: Fixed-size windows are common in this project (e.g., Main Window is locked to `600x800`) to ensure pixel-perfect placement of elements.

#### 2. Iconography
- **Format**: SVG (Scalable Vector Graphics).
- **Location**: `ui/icons/GREY/`
- **Style**: Monochromatic (Grey/Silver) icons that transition to blue or white depending on the element's state.
- **Resource System**: Icons are managed via `.qrc` files and compiled into `*_rc.py` modules.

#### 3. Styling Strategy
Styles are centralized in `all_styles.py` as string constants (e.g., `SETTINGS_MAIN`, `MSG_MAIN`). This allows for:
- Consistency across dynamically created dialogs.
- Easy global theme updates.

---

### Window Layouts

#### Main Window (`ui_MainWindow.ui`)
- **Structure**: A vertical stack (`QVBoxLayout`) of large, wide buttons.
- **Header-less Design**: Uses the buttons themselves as the primary navigation.
- **Bottom Bar**: A horizontal layout (`QHBoxLayout`) containing secondary actions like "Settings" and "Exit".

#### Settings Dialog (`ui_07_settings.ui`)
- **Structure**: Uses `QTabWidget` for categorized settings.
- **Controls**: A mix of `QLineEdit` (for paths) and `QPushButton` for folder selection.
- **Feedback**: Integrated `QToolButton` for info tooltips.

#### Message Boxes (Customized `QMessageBox`)
- Instead of standard Windows dialogs, the project uses customized `QMessageBox` with specific icons (`msg_info.svg`, `msg_warn.svg`) and large, styled buttons.

---

### Design Implementation Guidelines for Other Projects

1.  **Centralize Styles**: Do not hardcode colors in Python logic. Use a style module like `all_styles.py`.
2.  **Use SVGs**: Ensure all icons are SVG to maintain clarity across different screen DPIs.
3.  **Consistent Radii**: Use consistent `border-radius` (e.g., 15px) for all containers and buttons.
4.  **Interactive Feedback**: Always define `:hover` and `:pressed` states in QSS to provide visual confirmation to the user.
5.  **Font Embedding**: Ensure "Lexend Light" is either installed on the target system or bundled with the application resources.
