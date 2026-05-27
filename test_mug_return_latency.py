import time
import requests

from selenium import webdriver
from selenium.webdriver.common.by import By
from selenium.webdriver.support.ui import WebDriverWait
from selenium.webdriver.support import expected_conditions as EC

BASE_URL = "http://127.0.0.1:8000"


def main():
    driver = webdriver.Chrome()
    driver.set_window_size(420, 800)
    print("Chrome opened. Running mug return latency test...")

    # 1. Open Mug Exchange SPA
    driver.get(BASE_URL)

    # 2. Log in as demo
    WebDriverWait(driver, 5).until(
        EC.visibility_of_element_located((By.ID, "login-username"))
    ).send_keys("demo")
    driver.find_element(By.ID, "login-btn").click()

    WebDriverWait(driver, 5).until(
        EC.visibility_of_element_located((By.ID, "welcome"))
    )
    print("Login success, dashboard loaded.")

    # 3. Simulate mug assignment on frontend
    driver.execute_script("if (window.demoAssign) { window.demoAssign(); }")
    print("Simulated mug assignment via window.demoAssign().")
    time.sleep(5)

    # 4. Go to History and count baseline entries
    driver.find_element(By.CSS_SELECTOR, '[data-nav="past"]').click()
    WebDriverWait(driver, 5).until(
        EC.visibility_of_element_located((By.ID, "past-list"))
    )
    initial_items = len(driver.find_elements(By.CSS_SELECTOR, "#past-list .card.item"))
    print(f"Initial history entries: {initial_items}")

    # 5. Trigger backend return event
    t0 = time.perf_counter()
    resp = requests.post(f"{BASE_URL}/mock-return", timeout=3)
    resp.raise_for_status()

    # 6. Wait for frontend to pick up and render new history entry
    def history_updated(drv):
        items = drv.find_elements(By.CSS_SELECTOR, "#past-list .card.item")
        return len(items) == initial_items + 1

    try:
        WebDriverWait(driver, 5).until(history_updated)
        t1 = time.perf_counter()
        latency_ms = (t1 - t0) * 1000.0
        print(f"Mug return end-to-end latency: {latency_ms:.2f} ms")
    except Exception:
        print("Test FAILED: History did not update within timeout.")

    # 7. Keep Chrome open for manual check (5 seconds)
    print("Check the History tab in Chrome. Closing in 5 seconds...")
    time.sleep(5)

    # 8. Close browser
    driver.quit()
    print("Browser closed. Test complete.")


if __name__ == "__main__":
    main()
