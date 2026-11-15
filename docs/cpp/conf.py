# Sphinx configuration for the saiopt C++ guide.
# Build with: sphinx-build -b html docs/cpp docs/cpp/_build/html

project = "saiopt for C++"
author = "saiopt contributors"
copyright = "2026, saiopt contributors"
release = "0.1.0"

extensions = []
exclude_patterns = ["_build"]

primary_domain = "cpp"
highlight_language = "cpp"
html_theme = "furo"
html_title = "saiopt for C++"
