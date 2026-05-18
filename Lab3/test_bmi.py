import lab2_sub.bmi as bmi

def test_bmi_normal_weight():
        result = bmi.calculate_bmi()
        assert result == 0

def test_bmi_under_weight():
        result = bmi.calculate_bmi()
        assert result == -1

def test_bmi_over_weight():
        result = bmi.calculate_bmi()
        assert result == 1

