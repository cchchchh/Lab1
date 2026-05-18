import price_info

def test_total_cost_shopping():
    expected_total = 46.75
    result = price_info.total_cost_shopping()

    assert result == expected_total

def test_cost_of_fruit():
    result = price_info.cost_of_fruits('apple', 10)
    
    assert result == 12.0