from playwright.sync_api import sync_playwright

def run_cuj(page):
    page.goto("http://localhost:8000")
    page.wait_for_timeout(1000)

    # Click Ativar Áudio
    page.click('button:has-text("Ativar Áudio")')
    page.wait_for_timeout(2000)

    # Switch to Synth mode
    page.click('text=Synth Pluck')
    page.wait_for_timeout(500)

    # Click Play (actually it just has a play icon, but we can target the play button)
    page.locator('button.btn-transport.play-pause').click()
    page.wait_for_timeout(5000)

    # Take screenshot at the key moment
    page.screenshot(path="/home/jules/verification/screenshots/verification.png")
    page.wait_for_timeout(1000)

    # Click Pause
    page.locator('button.btn-transport.play-pause').click()
    page.wait_for_timeout(500)

if __name__ == "__main__":
    import os
    os.makedirs("/home/jules/verification/videos", exist_ok=True)
    os.makedirs("/home/jules/verification/screenshots", exist_ok=True)
    with sync_playwright() as p:
        browser = p.chromium.launch(headless=True)
        context = browser.new_context(
            record_video_dir="/home/jules/verification/videos"
        )
        page = context.new_page()
        try:
            run_cuj(page)
        finally:
            context.close()
            browser.close()
