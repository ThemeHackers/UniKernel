import subprocess
import argparse
import os
import time
from concurrent.futures import ThreadPoolExecutor
from rich.console import Console
from rich.panel import Panel
from rich.table import Table
from rich.progress import Progress, SpinnerColumn, TextColumn, BarColumn, TimeElapsedColumn
from rich.live import Live

console = Console()

def run_command(cmd, desc):
    try:
        result = subprocess.run(cmd, capture_output=True, text=True)
        if result.returncode == 0:
            return True, result.stdout
        else:
            return False, result.stderr
    except Exception as e:
        return False, str(e)

def run_upload(port_num, progress, task_id):
    arduino_cli = ".\\tools\\arduino-cli.exe"
    fqbn = "esp8266:esp8266:nodemcuv2"
    sketch = "Unikernel.ino"
    com_port = f"COM{port_num}"
    
    progress.update(task_id, description=f"[yellow]Uploading to {com_port}...")
    
    cmd = [
        arduino_cli, "upload",
        "-p", com_port,
        "--fqbn", fqbn,
        sketch
    ]
    
    success, output = run_command(cmd, f"Uploading to {com_port}")
    if success:
        progress.update(task_id, description=f"[green]✔ {com_port} Done", completed=100)
        return {"port": com_port, "status": "Success", "message": "Uploaded latest build."}
    else:
        progress.update(task_id, description=f"[red]✘ {com_port} Failed", completed=100)
        return {"port": com_port, "status": "Failed", "message": output.strip().split('\n')[-1]}

def check_ch340_driver():
    try:
        result = subprocess.run(["wmic", "path", "Win32_PnPEntity", "get", "Caption"], capture_output=True, text=True)
        if "CH340" in result.stdout:
            return True, result.stdout
        return False, "CH340 Driver not found or device not connected."
    except Exception as e:
        return False, str(e)

def main():
    parser = argparse.ArgumentParser(description="UniKernel Smart Parallel Uploader")
    parser.add_argument("--cport", nargs="+", type=int, help="List of COM port numbers")
    parser.add_argument("--skip-compile", action="store_true", help="Skip compilation step")
    args = parser.parse_args()

    is_parallel = len(args.cport) > 1
    title = "UniKernel Smart Parallel Uploader" if is_parallel else "UniKernel Smart Uploader"
    console.print(Panel.fit(f"[bold cyan]{title}[/bold cyan]", border_style="blue"))

    if not args.cport:
        console.print("[yellow]Usage: python upload.py --cport 5 6[/yellow]")
        return


    with console.status("[bold blue]Stage 0: Checking CH340 Driver...") as status:
        success, output = check_ch340_driver()
        if not success:
            console.print("[bold red]⚠ Warning: CH340 Driver not detected![/bold red]")
            console.print("[dim]Make sure your devices are plugged in and drivers are installed.[/dim]")
    
        else:
            console.print("[green]✔ CH340 Driver detected.[/green]")

    arduino_cli = ".\\tools\\arduino-cli.exe"
    fqbn = "esp8266:esp8266:nodemcuv2"
    sketch = "Unikernel.ino"

    if not os.path.exists(arduino_cli):
        console.print("[bold red]Error:[/bold red] arduino-cli.exe not found in .\\tools\\")
        return

    if not args.skip_compile:
        start_time = time.time()
        with console.status("[bold green]Stage 1: Compiling latest code...") as status:
            compile_cmd = [arduino_cli, "compile", "--fqbn", fqbn, sketch]
            success, output = run_command(compile_cmd, "Building binary")
            
            if not success:
                console.print(Panel(f"[red]{output}[/red]", title="[bold red]Compilation Error[/bold red]"))
                return
            
            duration = round(time.time() - start_time, 2)
            console.print(f"[green]✔ Compilation finished in {duration}s[/green]")
    else:
        console.print("[blue]ℹ Stage 1: Skipping compilation (using previous build)[/blue]")

    up_type = "parallel upload" if is_parallel else "upload"
    console.print(f"\n[bold]Stage 2: Starting {up_type} to: {', '.join([f'COM{p}' for p in args.cport])}[/bold]")
    
    results = []
    with Progress(
        SpinnerColumn(),
        TextColumn("[progress.description]{task.description}"),
        BarColumn(),
        TimeElapsedColumn(),
        console=console
    ) as progress:
        
        with ThreadPoolExecutor(max_workers=len(args.cport)) as executor:
            futures = []
            for port in args.cport:
                task_id = progress.add_task(description=f"Preparing COM{port}...", total=100)
                futures.append(executor.submit(run_upload, port, progress, task_id))
            
            for future in futures:
                results.append(future.result())


    table = Table(title="Final Status Report", show_header=True, header_style="bold magenta")
    table.add_column("Port", style="cyan")
    table.add_column("Status", justify="center")
    table.add_column("Message")

    for res in results:
        status_style = "green" if res["status"] == "Success" else "red"
        table.add_row(
            res["port"], 
            f"[{status_style}]{res['status']}[/{status_style}]", 
            res["message"]
        )

    console.print("\n")
    console.print(table)

if __name__ == "__main__":
    main()
