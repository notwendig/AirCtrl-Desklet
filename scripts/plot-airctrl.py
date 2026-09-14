#!/usr/bin/env python3
"""Create a headless, multi-page PDF from an AirControl status CSV file."""

from __future__ import annotations

import argparse
import csv
from datetime import datetime, timezone
import math
from pathlib import Path
import sys
import tempfile


DEFAULT_INPUT = Path("/var/log/airctrl.log")
DEFAULT_OUTPUT = Path("airctrl-status.pdf")
TEXT_COLUMNS = {"timestamp", "DeviceId", "ProductId", "_extra_json"}
BOOLEAN_COLUMNS = {"pwr", "cl", "uil", "otacheck", "wifilog"}
LABELS = {
    "pm25": "PM2,5 (µg/m³)",
    "rh": "Relative Luftfeuchte (%)",
    "rhset": "Zielfeuchte (%)",
    "temp": "Temperatur (°C)",
    "rssi": "WLAN-Signal (dBm)",
    "fltsts0": "Vorfilter-Restzeit (h)",
    "fltsts1": "HEPA-Restzeit (h)",
    "fltsts2": "Aktivkohle-Restzeit (h)",
    "wicksts": "Docht-Restzeit (h)",
    "Runtime": "Gerätelaufzeit (ms)",
    "free_memory": "Freier Gerätespeicher (Rohwert)",
    "aqil": "Lichtringhelligkeit (%)",
    "dt": "Abschalttimer (h)",
    "dtrs": "Timer-Restzeit (min, beobachtet)",
    "pwr": "Gerät",
    "cl": "Kindersicherung",
    "uil": "Displaybeleuchtung",
    "otacheck": "Firmware-Prüfung",
    "wifilog": "WLAN-Protokollierung",
}


class PlotError(Exception):
    """An input or output error that should be shown without a traceback."""


def parse_timestamp(value: str) -> datetime:
    value = value.strip()
    if value.endswith("Z"):
        value = value[:-1] + "+00:00"
    parsed = datetime.fromisoformat(value)
    if parsed.tzinfo is None:
        parsed = parsed.replace(tzinfo=timezone.utc)
    return parsed.astimezone(timezone.utc)


def read_status(path: Path, requested: set[str] | None):
    if path.is_symlink() or not path.is_file():
        raise PlotError(f"Eingabe ist keine reguläre Datei: {path}")

    with path.open(newline="", encoding="utf-8-sig") as stream:
        reader = csv.DictReader(stream)
        headers = reader.fieldnames
        if not headers or headers[0] != "timestamp":
            raise PlotError("Die CSV-Kopfzeile muss mit 'timestamp' beginnen.")
        if len(headers) != len(set(headers)):
            raise PlotError("Die CSV-Kopfzeile enthält doppelte Spaltennamen.")

        rows: list[tuple[datetime, dict[str, str]]] = []
        invalid_timestamps = 0
        for row in reader:
            if None in row:
                continue
            try:
                stamp = parse_timestamp(row.get("timestamp", ""))
            except (TypeError, ValueError):
                invalid_timestamps += 1
                continue
            rows.append((stamp, row))

    if not rows:
        raise PlotError("Keine gültigen Statuszeilen mit Zeitstempel gefunden.")

    columns: list[tuple[str, list[float]]] = []
    for name in headers[1:]:
        if name in TEXT_COLUMNS or (requested is not None and name not in requested):
            continue
        values: list[float] = []
        numeric_count = 0
        for _, row in rows:
            raw = row.get(name, "").strip()
            if not raw:
                values.append(math.nan)
                continue
            try:
                value = float(raw)
            except ValueError:
                values.append(math.nan)
                continue
            if not math.isfinite(value):
                values.append(math.nan)
                continue
            values.append(value)
            numeric_count += 1
        if numeric_count:
            columns.append((name, values))

    if requested is not None:
        unknown = requested.difference(headers)
        if unknown:
            raise PlotError("Unbekannte Spalte(n): " + ", ".join(sorted(unknown)))
    if not columns:
        raise PlotError("Keine numerischen Statuswerte gefunden.")
    return rows, columns, invalid_timestamps


def plot_pdf(input_path: Path, output_path: Path, per_page: int,
             requested: set[str] | None) -> tuple[int, int, int]:
    try:
        import matplotlib
        matplotlib.use("Agg")
        import matplotlib.dates as mdates
        import matplotlib.pyplot as plt
        from matplotlib.backends.backend_pdf import PdfPages
    except ImportError as error:
        raise PlotError(
            "Matplotlib fehlt. Fedora: sudo dnf install python3-matplotlib") from error

    rows, columns, invalid_timestamps = read_status(input_path, requested)
    resolved_input = input_path.resolve()
    if output_path.exists() and output_path.is_symlink():
        raise PlotError(f"Ausgabe darf kein symbolischer Link sein: {output_path}")
    if output_path.resolve() == resolved_input:
        raise PlotError("Eingabe und Ausgabe dürfen nicht identisch sein.")
    output_path.parent.mkdir(parents=True, exist_ok=True)

    stamps = [stamp for stamp, _ in rows]
    pages = math.ceil(len(columns) / per_page)
    temporary_name: str | None = None
    try:
        with tempfile.NamedTemporaryFile(
                prefix=f".{output_path.name}.", suffix=".tmp",
                dir=output_path.parent, delete=False) as temporary:
            temporary_name = temporary.name
        with PdfPages(temporary_name, metadata={
                "Title": "AirControl-Status",
                "Author": "AirCtrl-Desklet",
                "Subject": f"Statuswerte aus {input_path.name}",
        }) as pdf:
            for page_index in range(pages):
                figure, axes = plt.subplots(
                    per_page, 1, figsize=(8.27, 11.69), squeeze=False)
                flat_axes = axes[:, 0]
                begin = page_index * per_page
                page_columns = columns[begin:begin + per_page]
                for axis, (name, values) in zip(flat_axes, page_columns):
                    axis.plot(stamps, values, color="#1464a5", linewidth=1.4,
                              marker="o", markersize=2.5)
                    axis.set_title(f"{LABELS.get(name, name)}  [{name}]", fontsize=11)
                    axis.set_ylabel("Wert")
                    axis.set_xlabel("Zeit (UTC)")
                    axis.grid(True, color="#d5d9dd", linewidth=0.6)
                    locator = mdates.AutoDateLocator(minticks=3, maxticks=8)
                    axis.xaxis.set_major_locator(locator)
                    axis.xaxis.set_major_formatter(mdates.ConciseDateFormatter(locator))
                    if name in BOOLEAN_COLUMNS:
                        axis.set_yticks([0, 1], labels=["Aus", "Ein"])
                        axis.set_ylim(-0.15, 1.15)
                    else:
                        finite = [value for value in values if math.isfinite(value)]
                        if finite and min(finite) == max(finite):
                            padding = max(abs(finite[0]) * 0.05, 1.0)
                            axis.set_ylim(finite[0] - padding, finite[0] + padding)
                for axis in flat_axes[len(page_columns):]:
                    axis.set_visible(False)
                figure.suptitle(
                    f"AirControl-Status · Seite {page_index + 1}/{pages}", fontsize=14)
                figure.tight_layout(rect=(0.02, 0.02, 0.98, 0.97))
                pdf.savefig(figure)
                plt.close(figure)
        Path(temporary_name).replace(output_path)
        temporary_name = None
    finally:
        if temporary_name is not None:
            Path(temporary_name).unlink(missing_ok=True)
    return len(rows), len(columns), invalid_timestamps


def main() -> int:
    parser = argparse.ArgumentParser(
        description="AirControl-CSV als mehrseitige PDF mit vier Diagrammen pro Seite")
    parser.add_argument("input", type=Path, nargs="?", default=DEFAULT_INPUT,
                        help=f"CSV-Eingabe (Standard: {DEFAULT_INPUT})")
    parser.add_argument("output", type=Path, nargs="?", default=DEFAULT_OUTPUT,
                        help=f"PDF-Ausgabe (Standard: {DEFAULT_OUTPUT})")
    parser.add_argument("--per-page", type=int, choices=range(1, 9), default=4,
                        metavar="1..8", help="Diagramme pro PDF-Seite (Standard: 4)")
    parser.add_argument("--fields", help="Kommagetrennte Auswahl von CSV-Spalten")
    args = parser.parse_args()
    if args.output.suffix.lower() != ".pdf":
        parser.error("Die Ausgabe muss die Endung .pdf haben.")
    requested = None
    if args.fields is not None:
        requested = {field.strip() for field in args.fields.split(",") if field.strip()}
        if not requested:
            parser.error("--fields enthält keine Spaltennamen.")
    try:
        rows, columns, invalid = plot_pdf(
            args.input, args.output, args.per_page, requested)
    except (OSError, PlotError) as error:
        print(f"FEHLER: {error}", file=sys.stderr)
        return 1
    pages = math.ceil(columns / args.per_page)
    suffix = f"; {invalid} ungültige Zeitstempel übersprungen" if invalid else ""
    print(f"PDF geschrieben: {args.output} "
          f"({columns} Werte, {pages} Seiten, {rows} Statuszeilen{suffix})")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
